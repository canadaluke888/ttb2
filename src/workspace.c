#include "workspace.h"
#include "ttb_io.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#define WORKSPACE_DIR "workspace"
#define WORKSPACE_SESSION_BOOK WORKSPACE_DIR "/session.ttbx"
#define WORKSPACE_DEFAULT_BOOK_NAME "Untitled Book"
#define WORKSPACE_DEFAULT_TABLE_NAME "Untitled Table"

static char g_project_path[PATH_MAX] = WORKSPACE_SESSION_BOOK;
static char g_active_table_id[256] = "";
static char g_book_name[256] = WORKSPACE_DEFAULT_BOOK_NAME;
static int g_autosave_on = 1;
static Table *g_active_table = NULL;
static int g_book_active = 1;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int ensure_workspace_dir(char *err, size_t err_sz)
{
    struct stat st;
    if (stat(WORKSPACE_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        set_err(err, err_sz, "workspace path is not a directory");
        return -1;
    }
    if (mkdir(WORKSPACE_DIR, 0755) != 0) {
        if (errno == EEXIST) return 0;
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

static int refresh_workspace_meta(char *err, size_t err_sz)
{
    TtbxManifest manifest;
    if (ttbx_manifest_load(g_project_path, &manifest, err, err_sz) != 0) return -1;
    copy_book_name(manifest.book_name);
    copy_active_table_id(manifest.active_table_id);
    ttbx_manifest_free(&manifest);
    return 0;
}

static int ensure_session_book(const Table *seed_table, char *err, size_t err_sz)
{
    Table *tmp = NULL;
    const Table *src = seed_table;

    if (ttbx_is_book_dir(g_project_path)) {
        return 0;
    }

    if (!src) {
        tmp = create_table(WORKSPACE_DEFAULT_TABLE_NAME);
        if (!tmp) {
            set_err(err, err_sz, "Failed to allocate table");
            return -1;
        }
        src = tmp;
    }

    if (ttbx_remove_book(g_project_path, NULL, 0) != 0) {
        free_table(tmp);
        set_err(err, err_sz, "Failed to reset workspace book");
        return -1;
    }
    if (ttbx_save(src, g_project_path, err, err_sz) != 0) {
        free_table(tmp);
        return -1;
    }
    free_table(tmp);
    if (workspace_rename_book(WORKSPACE_DEFAULT_BOOK_NAME, err, err_sz) != 0) return -1;
    return refresh_workspace_meta(err, err_sz);
}

static int save_project(const Table *table, char *err, size_t err_sz)
{
    if (!table) {
        set_err(err, err_sz, "No table to save");
        return -1;
    }
    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    if (ensure_session_book(table, err, err_sz) != 0) return -1;
    if (ttbx_save_table(table, g_project_path, g_active_table_id, err, err_sz) != 0) {
        return -1;
    }
    return refresh_workspace_meta(err, err_sz);
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
    g_book_active = 1;
}

int workspace_book_is_active(void)
{
    return g_book_active;
}

int workspace_rename_book(const char *name, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    char *new_name;

    if (ensure_session_book(g_active_table, err, err_sz) != 0) return -1;
    if (ttbx_manifest_load(g_project_path, &manifest, err, err_sz) != 0) return -1;
    new_name = strdup((name && *name) ? name : WORKSPACE_DEFAULT_BOOK_NAME);
    if (!new_name) {
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    free(manifest.book_name);
    manifest.book_name = new_name;
    if (ttbx_manifest_save(g_project_path, &manifest, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }
    copy_book_name(manifest.book_name);
    ttbx_manifest_free(&manifest);
    return 0;
}

int workspace_open_book(Table *table, const char *path, char *err, size_t err_sz)
{
    Table *loaded;
    if (!table || !path) {
        set_err(err, err_sz, "Invalid book open request");
        return -1;
    }
    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    if (ttbx_copy_book(path, g_project_path, err, err_sz) != 0) return -1;
    if (refresh_workspace_meta(err, err_sz) != 0) return -1;
    loaded = ttbx_load_table(g_project_path, g_active_table_id, err, err_sz);
    if (!loaded) return -1;
    replace_table_contents(table, loaded);
    workspace_set_active_table(table);
    return 0;
}

int workspace_switch_table(Table *table, const char *table_id, char *err, size_t err_sz)
{
    Table *loaded;
    if (!table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    if (g_active_table_id[0] && strcmp(g_active_table_id, table_id) == 0) return 0;
    if (save_project(table, err, err_sz) != 0) return -1;
    loaded = ttbx_load_table(g_project_path, table_id, err, err_sz);
    if (!loaded) return -1;
    replace_table_contents(table, loaded);
    copy_active_table_id(table_id);
    if (save_project(table, err, err_sz) != 0) return -1;
    workspace_set_active_table(table);
    return 0;
}

int workspace_new_table(Table *table, char *err, size_t err_sz)
{
    if (!table) {
        set_err(err, err_sz, "No table");
        return -1;
    }
    if (save_project(table, err, err_sz) != 0) return -1;
    clear_table(table, WORKSPACE_DEFAULT_TABLE_NAME);
    copy_active_table_id("");
    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    if (ttbx_save_table(table, g_project_path, "", err, err_sz) != 0) return -1;
    if (refresh_workspace_meta(err, err_sz) != 0) return -1;
    workspace_set_active_table(table);
    return 0;
}

int workspace_export_book(const char *path, char *err, size_t err_sz)
{
    if (!path || !*path) {
        set_err(err, err_sz, "No destination provided");
        return -1;
    }
    if (g_active_table && save_project(g_active_table, err, err_sz) != 0) return -1;
    return ttbx_copy_book(g_project_path, path, err, err_sz);
}

int workspace_list_book_tables(char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    char **names = NULL;
    char **ids = NULL;

    if (names_out) *names_out = NULL;
    if (ids_out) *ids_out = NULL;
    if (count_out) *count_out = 0;

    if (ensure_session_book(g_active_table, err, err_sz) != 0) return -1;
    if (ttbx_manifest_load(g_project_path, &manifest, err, err_sz) != 0) return -1;

    names = (char **)calloc(manifest.table_count, sizeof(char *));
    ids = (char **)calloc(manifest.table_count, sizeof(char *));
    if (!names || !ids) {
        free(names);
        free(ids);
        ttbx_manifest_free(&manifest);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    for (int i = 0; i < manifest.table_count; ++i) {
        names[i] = strdup(manifest.tables[i].name ? manifest.tables[i].name : "");
        ids[i] = strdup(manifest.tables[i].id ? manifest.tables[i].id : "");
    }
    if (names_out) *names_out = names;
    if (ids_out) *ids_out = ids;
    if (count_out) *count_out = manifest.table_count;
    ttbx_manifest_free(&manifest);
    return 0;
}

int workspace_autosave(const Table *table, char *err, size_t err_sz)
{
    if (!g_autosave_on) return 0;
    workspace_set_active_table((Table *)table);
    return save_project(table, err, err_sz);
}

int workspace_manual_save(const Table *table, char *err, size_t err_sz)
{
    workspace_set_active_table((Table *)table);
    return save_project(table, err, err_sz);
}

int workspace_init(Table **out_table, char *err, size_t err_sz)
{
    Table *table = NULL;

    if (ensure_workspace_dir(err, err_sz) != 0) return -1;
    workspace_clear_book();
    if (ensure_session_book(NULL, err, err_sz) != 0) return -1;
    if (refresh_workspace_meta(err, err_sz) != 0) return -1;

    table = ttbx_load_table(g_project_path, g_active_table_id, err, err_sz);
    if (!table) return -1;
    workspace_set_active_table(table);
    if (out_table) *out_table = table;
    return 0;
}

void workspace_shutdown(void)
{
    ttbx_remove_book(WORKSPACE_SESSION_BOOK, NULL, 0);
}
