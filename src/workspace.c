/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Workspace lifecycle, autosave, and book coordination helpers. */

#include "core/workspace.h"
#include "db/book_db.h"
#include "io/ttb_io.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WORKSPACE_DIR "workspace"
#define WORKSPACE_SESSION_BOOK WORKSPACE_DIR "/session.ttbx"
#define WORKSPACE_DEFAULT_BOOK_NAME "Untitled Book"
#define WORKSPACE_DEFAULT_TABLE_NAME "Untitled Table"
#define WORKSPACE_AUTOSAVE_IDLE_MS 400

static char g_project_path[PATH_MAX] = WORKSPACE_SESSION_BOOK;
static char g_active_table_id[256] = "";
static char g_book_name[256] = WORKSPACE_DEFAULT_BOOK_NAME;
static int g_autosave_on = 1;
static Table *g_active_table = NULL;
static Table *g_saved_snapshot = NULL;
static int g_book_active = 1;
static int g_autosave_pending = 0;
static long long g_autosave_due_ms = 0;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static long long monotonic_millis(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static void clear_saved_snapshot(void)
{
    if (g_saved_snapshot) {
        free_table(g_saved_snapshot);
        g_saved_snapshot = NULL;
    }
}

static int capture_saved_snapshot(const Table *table, char *err, size_t err_sz)
{
    Table *copy = NULL;

    if (table) {
        copy = clone_table(table);
        if (!copy) {
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    clear_saved_snapshot();
    g_saved_snapshot = copy;
    return 0;
}

static int path_exists(const char *path)
{
    struct stat st;
    return (path && stat(path, &st) == 0) ? 1 : 0;
}

static int path_is_directory(const char *path)
{
    struct stat st;
    return (path && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

static int path_is_regular_file(const char *path)
{
    struct stat st;
    return (path && stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

static int remove_path_recursive(const char *path, char *err, size_t err_sz)
{
    DIR *dir = NULL;
    struct dirent *entry;

    if (!path || !*path || !path_exists(path)) return 0;
    if (!path_is_directory(path)) {
        if (unlink(path) != 0 && errno != ENOENT) {
            set_err(err, err_sz, strerror(errno));
            return -1;
        }
        return 0;
    }

    dir = opendir(path);
    if (!dir) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (remove_path_recursive(child, err, err_sz) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0 && errno != ENOENT) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst, char *err, size_t err_sz)
{
    FILE *in = NULL;
    FILE *out = NULL;
    char buf[8192];
    size_t nread;

    if (!src || !dst) {
        set_err(err, err_sz, "Invalid copy path");
        return -1;
    }
    if (strcmp(src, dst) == 0) return 0;

    in = fopen(src, "rb");
    if (!in) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        set_err(err, err_sz, strerror(errno));
        return -1;
    }

    while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, nread, out) != nread) {
            fclose(in);
            fclose(out);
            set_err(err, err_sz, strerror(errno));
            return -1;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        set_err(err, err_sz, strerror(errno));
        return -1;
    }
    return 0;
}

static int ensure_workspace_dir(char *err, size_t err_sz)
{
    if (mkdir(WORKSPACE_DIR, 0755) != 0) {
        if (errno == EEXIST && path_is_directory(WORKSPACE_DIR)) return 0;
        set_err(err, err_sz, "failed to create workspace directory");
        return -1;
    }
    return 0;
}

static void copy_project_path(const char *path)
{
    strncpy(g_project_path, path ? path : WORKSPACE_SESSION_BOOK, sizeof(g_project_path) - 1);
    g_project_path[sizeof(g_project_path) - 1] = '\0';
}

static void copy_active_table_id(const char *table_id)
{
    strncpy(g_active_table_id, table_id ? table_id : "", sizeof(g_active_table_id) - 1);
    g_active_table_id[sizeof(g_active_table_id) - 1] = '\0';
}

static void copy_book_name(const char *name)
{
    strncpy(g_book_name, (name && *name) ? name : WORKSPACE_DEFAULT_BOOK_NAME, sizeof(g_book_name) - 1);
    g_book_name[sizeof(g_book_name) - 1] = '\0';
}

static int refresh_workspace_meta(BookDB *db, char *err, size_t err_sz)
{
    char active_id[256];
    char book_name[256];

    if (!db) {
        set_err(err, err_sz, "Book DB is not open");
        return -1;
    }
    if (bookdb_get_book_name(db, book_name, sizeof(book_name), err, err_sz) != 0) return -1;
    if (bookdb_get_active_table_id(db, active_id, sizeof(active_id), err, err_sz) != 0) return -1;
    copy_book_name(book_name);
    copy_active_table_id(active_id);
    return 0;
}

static int ensure_book_has_active_table(BookDB *db, const Table *seed_table, char *err, size_t err_sz)
{
    char **names = NULL;
    char **ids = NULL;
    int count = 0;
    char active_id[256];
    Table *tmp = NULL;
    const Table *src = seed_table;
    char created_id[256];
    int rc = -1;

    if (bookdb_list_tables(db, &names, &ids, &count, err, err_sz) != 0) return -1;
    if (count == 0) {
        if (!src) {
            tmp = create_table(WORKSPACE_DEFAULT_TABLE_NAME);
            if (!tmp) {
                set_err(err, err_sz, "Out of memory");
                free(names);
                free(ids);
                return -1;
            }
            src = tmp;
        }
        rc = bookdb_create_table(db, src, created_id, sizeof(created_id), err, err_sz);
        free_table(tmp);
        free(names);
        free(ids);
        if (rc != 0) return -1;
        return bookdb_set_active_table_id(db, created_id, err, err_sz);
    }

    for (int i = 0; i < count; ++i) {
        free(names[i]);
        free(ids[i]);
    }
    free(names);
    free(ids);

    if (bookdb_get_active_table_id(db, active_id, sizeof(active_id), err, err_sz) != 0) return -1;
    if (!active_id[0]) {
        if (bookdb_list_tables(db, &names, &ids, &count, err, err_sz) != 0) return -1;
        rc = (count > 0) ? bookdb_set_active_table_id(db, ids[0], err, err_sz) : 0;
        for (int i = 0; i < count; ++i) {
            free(names[i]);
            free(ids[i]);
        }
        free(names);
        free(ids);
        return rc;
    }
    return 0;
}

static int ensure_session_book(const Table *seed_table, char *err, size_t err_sz)
{
    BookDB *db = NULL;

    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    if (path_is_directory(g_project_path)) {
        if (remove_path_recursive(g_project_path, err, err_sz) != 0) return -1;
    }
    if (!path_exists(g_project_path)) {
        if (bookdb_create_empty(g_project_path, g_book_name, err, err_sz) != 0) return -1;
    }

    db = bookdb_open(g_project_path, 1, err, err_sz);
    if (!db) return -1;
    if (bookdb_init_schema(db, err, err_sz) != 0 ||
        ensure_book_has_active_table(db, seed_table, err, err_sz) != 0 ||
        refresh_workspace_meta(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    return 0;
}

static int open_session_db(BookDB **out_db, const Table *seed_table, char *err, size_t err_sz)
{
    BookDB *db;

    if (out_db) *out_db = NULL;
    if (ensure_session_book(seed_table, err, err_sz) != 0) return -1;
    db = bookdb_open(g_project_path, 0, err, err_sz);
    if (!db) return -1;
    if (bookdb_init_schema(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    if (out_db) *out_db = db;
    return 0;
}

static int save_project(const Table *table, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    char created_id[256];

    if (!table) {
        set_err(err, err_sz, "No table to save");
        return -1;
    }
    if (open_session_db(&db, table, err, err_sz) != 0) return -1;
    if (g_active_table_id[0]) {
        if (bookdb_save_table_incremental(db, g_active_table_id, g_saved_snapshot, table, err, err_sz) != 0) {
            bookdb_close(db);
            return -1;
        }
    } else {
        if (bookdb_create_table(db, table, created_id, sizeof(created_id), err, err_sz) != 0 ||
            bookdb_set_active_table_id(db, created_id, err, err_sz) != 0) {
            bookdb_close(db);
            return -1;
        }
    }
    if (refresh_workspace_meta(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    return 0;
}

int workspace_set_project_path(const char *path)
{
    copy_project_path(path && *path ? path : WORKSPACE_SESSION_BOOK);
    return 0;
}

const char *workspace_project_path(void)
{
    return g_project_path;
}

void workspace_set_autosave_enabled(int enabled)
{
    g_autosave_on = enabled ? 1 : 0;
}

int workspace_autosave_enabled(void)
{
    return g_autosave_on;
}

void workspace_set_active_table(Table *table)
{
    g_active_table = table;
}

const char *workspace_active_table_id(void)
{
    return g_active_table_id;
}

const char *workspace_book_name(void)
{
    return g_book_name;
}

void workspace_clear_book(void)
{
    copy_project_path(WORKSPACE_SESSION_BOOK);
    copy_book_name(WORKSPACE_DEFAULT_BOOK_NAME);
    copy_active_table_id("");
    clear_saved_snapshot();
    g_book_active = 1;
}

int workspace_book_is_active(void)
{
    return g_book_active;
}

int workspace_rename_book(const char *name, char *err, size_t err_sz)
{
    BookDB *db = NULL;

    if (open_session_db(&db, g_active_table, err, err_sz) != 0) return -1;
    if (bookdb_set_book_name(db, name ? name : WORKSPACE_DEFAULT_BOOK_NAME, err, err_sz) != 0 ||
        refresh_workspace_meta(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    return 0;
}

int workspace_open_book(Table *table, const char *path, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    Table *loaded = NULL;

    if (!table || !path) {
        set_err(err, err_sz, "Invalid book open request");
        return -1;
    }
    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    if (remove_path_recursive(g_project_path, err, err_sz) != 0) return -1;

    if (ttbx_is_book_dir(path)) {
        if (bookdb_import_legacy_ttbx(path, g_project_path, err, err_sz) != 0) return -1;
    } else if (path_is_regular_file(path)) {
        if (copy_file(path, g_project_path, err, err_sz) != 0) return -1;
    } else {
        set_err(err, err_sz, "Unsupported book path");
        return -1;
    }

    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;
    loaded = bookdb_load_table(db, NULL, err, err_sz);
    if (!loaded) {
        bookdb_close(db);
        return -1;
    }
    if (refresh_workspace_meta(db, err, err_sz) != 0) {
        free_table(loaded);
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);

    replace_table_contents(table, loaded);
    workspace_set_active_table(table);
    if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    return 0;
}

int workspace_switch_table(Table *table, const char *table_id, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    Table *loaded = NULL;

    if (!table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    if (g_active_table_id[0] && strcmp(g_active_table_id, table_id) == 0) return 0;
    if (save_project(table, err, err_sz) != 0) return -1;
    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;
    if (bookdb_set_active_table_id(db, table_id, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    loaded = bookdb_load_table(db, table_id, err, err_sz);
    if (!loaded) {
        bookdb_close(db);
        return -1;
    }
    if (refresh_workspace_meta(db, err, err_sz) != 0) {
        free_table(loaded);
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    replace_table_contents(table, loaded);
    workspace_set_active_table(table);
    if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    return 0;
}

int workspace_new_table(Table *table, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    Table *fresh = NULL;
    char table_id[256];

    if (!table) {
        set_err(err, err_sz, "No table");
        return -1;
    }
    if (save_project(table, err, err_sz) != 0) return -1;
    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;

    fresh = create_table(WORKSPACE_DEFAULT_TABLE_NAME);
    if (!fresh) {
        bookdb_close(db);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    if (bookdb_create_table(db, fresh, table_id, sizeof(table_id), err, err_sz) != 0 ||
        bookdb_set_active_table_id(db, table_id, err, err_sz) != 0 ||
        refresh_workspace_meta(db, err, err_sz) != 0) {
        free_table(fresh);
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    replace_table_contents(table, fresh);
    workspace_set_active_table(table);
    if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    return 0;
}

int workspace_rename_table(Table *table, const char *table_id, const char *name, char *err, size_t err_sz)
{
    BookDB *db = NULL;

    if (!table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    if (!name || !*name) {
        set_err(err, err_sz, "No table name provided");
        return -1;
    }
    if (save_project(table, err, err_sz) != 0) return -1;
    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;
    if (bookdb_rename_table(db, table_id, name, err, err_sz) != 0 ||
        refresh_workspace_meta(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);

    if (g_active_table_id[0] && strcmp(g_active_table_id, table_id) == 0 && table) {
        char *new_name = strdup(name);
        if (!new_name) {
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
        free(table->name);
        table->name = new_name;
        if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    }
    return 0;
}

int workspace_delete_table(Table *table, const char *table_id, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    char **names = NULL;
    char **ids = NULL;
    int count = 0;
    int deleting_active;
    int delete_index = -1;
    char next_id[256] = "";
    Table *loaded = NULL;
    Table *fresh = NULL;

    if (!table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    if (save_project(table, err, err_sz) != 0) return -1;
    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;
    if (bookdb_list_tables(db, &names, &ids, &count, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }

    deleting_active = g_active_table_id[0] && strcmp(g_active_table_id, table_id) == 0;
    for (int i = 0; i < count; ++i) {
        if (strcmp(ids[i], table_id) == 0) {
            delete_index = i;
            break;
        }
    }
    if (delete_index < 0) {
        for (int i = 0; i < count; ++i) {
            free(names[i]);
            free(ids[i]);
        }
        free(names);
        free(ids);
        bookdb_close(db);
        set_err(err, err_sz, "Book table not found");
        return -1;
    }

    if (deleting_active && count > 1) {
        int next_index = (delete_index < count - 1) ? delete_index + 1 : delete_index - 1;
        strncpy(next_id, ids[next_index], sizeof(next_id) - 1);
        next_id[sizeof(next_id) - 1] = '\0';
    }

    if (bookdb_delete_table(db, table_id, err, err_sz) != 0) {
        for (int i = 0; i < count; ++i) {
            free(names[i]);
            free(ids[i]);
        }
        free(names);
        free(ids);
        bookdb_close(db);
        return -1;
    }

    for (int i = 0; i < count; ++i) {
        free(names[i]);
        free(ids[i]);
    }
    free(names);
    free(ids);

    if (deleting_active) {
        if (count > 1) {
            if (bookdb_set_active_table_id(db, next_id, err, err_sz) != 0) {
                bookdb_close(db);
                return -1;
            }
            loaded = bookdb_load_table(db, next_id, err, err_sz);
            if (!loaded) {
                bookdb_close(db);
                return -1;
            }
        } else {
            char created_id[256];
            fresh = create_table(WORKSPACE_DEFAULT_TABLE_NAME);
            if (!fresh) {
                bookdb_close(db);
                set_err(err, err_sz, "Out of memory");
                return -1;
            }
            if (bookdb_create_table(db, fresh, created_id, sizeof(created_id), err, err_sz) != 0 ||
                bookdb_set_active_table_id(db, created_id, err, err_sz) != 0) {
                free_table(fresh);
                bookdb_close(db);
                return -1;
            }
        }
    }

    if (refresh_workspace_meta(db, err, err_sz) != 0) {
        if (loaded) free_table(loaded);
        if (fresh) free_table(fresh);
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);

    if (loaded) {
        replace_table_contents(table, loaded);
        workspace_set_active_table(table);
        if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    } else if (fresh) {
        replace_table_contents(table, fresh);
        workspace_set_active_table(table);
        if (capture_saved_snapshot(table, err, err_sz) != 0) return -1;
    }
    return 0;
}

int workspace_export_book(const char *path, char *err, size_t err_sz)
{
    if (!path || !*path) {
        set_err(err, err_sz, "No destination provided");
        return -1;
    }
    if (g_active_table && save_project(g_active_table, err, err_sz) != 0) return -1;
    if (remove_path_recursive(path, err, err_sz) != 0) return -1;
    return copy_file(g_project_path, path, err, err_sz);
}

int workspace_export_book_db(const char *path, char *err, size_t err_sz)
{
    if (!path || !*path) {
        set_err(err, err_sz, "No destination provided");
        return -1;
    }
    if (g_active_table && save_project(g_active_table, err, err_sz) != 0) return -1;
    if (remove_path_recursive(path, err, err_sz) != 0) return -1;
    return copy_file(g_project_path, path, err, err_sz);
}

int workspace_list_book_tables(char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    int rc;

    if (names_out) *names_out = NULL;
    if (ids_out) *ids_out = NULL;
    if (count_out) *count_out = 0;

    if (open_session_db(&db, g_active_table, err, err_sz) != 0) return -1;
    rc = bookdb_list_tables(db, names_out, ids_out, count_out, err, err_sz);
    bookdb_close(db);
    return rc;
}

int workspace_autosave(const Table *table, char *err, size_t err_sz)
{
    Table *mutable_table = (Table *)table;

    if (!g_autosave_on) return 0;
    g_autosave_pending = 0;
    g_autosave_due_ms = 0;
    workspace_set_active_table(mutable_table);
    if (save_project(table, err, err_sz) != 0) return -1;
    if (mutable_table) mutable_table->dirty = 0;
    return 0;
}

void workspace_queue_autosave(Table *table)
{
    workspace_set_active_table(table);
    if (!g_autosave_on || !table) return;
    g_autosave_pending = 1;
    g_autosave_due_ms = monotonic_millis() + WORKSPACE_AUTOSAVE_IDLE_MS;
}

int workspace_process_autosave(char *err, size_t err_sz)
{
    if (!g_autosave_pending || !g_autosave_on) return 0;
    if (monotonic_millis() < g_autosave_due_ms) return 0;
    return workspace_flush_autosave(err, err_sz);
}

int workspace_flush_autosave(char *err, size_t err_sz)
{
    if (!g_autosave_pending || !g_autosave_on) return 0;
    if (!g_active_table) {
        g_autosave_pending = 0;
        g_autosave_due_ms = 0;
        return 0;
    }
    g_autosave_pending = 0;
    g_autosave_due_ms = 0;
    return workspace_autosave(g_active_table, err, err_sz);
}

int workspace_manual_save(const Table *table, char *err, size_t err_sz)
{
    Table *mutable_table = (Table *)table;

    g_autosave_pending = 0;
    g_autosave_due_ms = 0;
    workspace_set_active_table(mutable_table);
    if (save_project(table, err, err_sz) != 0) return -1;
    if (mutable_table) mutable_table->dirty = 0;
    return 0;
}

int workspace_init(Table **out_table, char *err, size_t err_sz)
{
    BookDB *db = NULL;
    Table *table = NULL;

    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    workspace_clear_book();
    if (open_session_db(&db, NULL, err, err_sz) != 0) return -1;
    if (refresh_workspace_meta(db, err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    table = bookdb_load_table(db, NULL, err, err_sz);
    bookdb_close(db);
    if (!table) return -1;
    workspace_set_active_table(table);
    if (capture_saved_snapshot(table, err, err_sz) != 0) {
        free_table(table);
        return -1;
    }
    if (out_table) *out_table = table;
    return 0;
}

void workspace_shutdown(void)
{
    clear_saved_snapshot();
    remove_path_recursive(WORKSPACE_SESSION_BOOK, NULL, 0);
}
