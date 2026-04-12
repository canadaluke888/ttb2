#include "db/book_db.h"
#include "io/ttb_io.h"

#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOOKDB_FORMAT_VERSION "1"

struct BookDB {
    sqlite3 *conn;
    char path[PATH_MAX];
};

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int exec_sql(sqlite3 *conn, const char *sql, char *err, size_t err_sz)
{
    char *sqlite_err = NULL;

    if (sqlite3_exec(conn, sql, NULL, NULL, &sqlite_err) != SQLITE_OK) {
        set_err(err, err_sz, sqlite_err ? sqlite_err : "SQLite error");
        if (sqlite_err) sqlite3_free(sqlite_err);
        return -1;
    }
    if (sqlite_err) sqlite3_free(sqlite_err);
    return 0;
}

static char *sanitize_component(const char *src)
{
    size_t len = src ? strlen(src) : 0;
    char *out;

    if (len == 0) return strdup("table");
    out = (char *)malloc(len + 1);
    if (!out) return NULL;

    for (size_t i = 0; i < len; ++i) {
        char ch = src[i];
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            out[i] = ch;
        } else {
            out[i] = '_';
        }
    }
    out[len] = '\0';
    return out;
}

static void quote_ident(char *out, size_t out_sz, const char *name)
{
    size_t n = 0;

    if (!out || out_sz == 0) return;
    out[n++] = '"';
    for (const char *p = name; p && *p && n < out_sz - 2; ++p) {
        if (*p == '"' && n < out_sz - 2) out[n++] = '"';
        out[n++] = *p;
    }
    out[n++] = '"';
    out[n] = '\0';
}

static int table_exists(BookDB *db, const char *table_id, int *exists_out, int *sort_index_out, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (exists_out) *exists_out = 0;
    if (sort_index_out) *sort_index_out = -1;

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT sort_index FROM book_tables WHERE id = ?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, table_id ? table_id : "", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (exists_out) *exists_out = 1;
        if (sort_index_out) *sort_index_out = sqlite3_column_int(stmt, 0);
    } else if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int next_sort_index(BookDB *db, int *sort_index_out, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!sort_index_out) {
        set_err(err, err_sz, "Invalid args");
        return -1;
    }
    *sort_index_out = 0;

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT COALESCE(MAX(sort_index), -1) + 1 FROM book_tables;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *sort_index_out = sqlite3_column_int(stmt, 0);
    } else {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int storage_table_name(const char *table_id, char *out, size_t out_sz)
{
    char *sanitized = sanitize_component(table_id);
    int rc;

    if (!sanitized) return -1;
    rc = snprintf(out, out_sz, "tbl_%s", sanitized);
    free(sanitized);
    return (rc < 0 || (size_t)rc >= out_sz) ? -1 : 0;
}

static int set_meta(BookDB *db, const char *key, const char *value, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db->conn,
        "INSERT INTO book_meta(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, key ? key : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    return 0;
}

static int get_meta(BookDB *db, const char *key, char *out, size_t out_sz, const char *fallback, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    const char *value = fallback ? fallback : "";

    if (!out || out_sz == 0) {
        set_err(err, err_sz, "Invalid args");
        return -1;
    }
    out[0] = '\0';

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT value FROM book_meta WHERE key = ?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, key ? key : "", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        value = txt ? (const char *)txt : "";
    } else if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }
    strncpy(out, value, out_sz - 1);
    out[out_sz - 1] = '\0';
    sqlite3_finalize(stmt);
    return 0;
}

static int save_table_with_id(BookDB *db, const char *table_id, const Table *table, int preserve_sort_index, int explicit_sort_index, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    char storage_name[512];
    char q_storage_name[1024];
    int exists = 0;
    int sort_index = -1;
    int rc = -1;

    if (!db || !db->conn || !table || !table_id || !*table_id) {
        set_err(err, err_sz, "Invalid args");
        return -1;
    }
    if (storage_table_name(table_id, storage_name, sizeof(storage_name)) != 0) {
        set_err(err, err_sz, "Table id too long");
        return -1;
    }
    quote_ident(q_storage_name, sizeof(q_storage_name), storage_name);

    if (table_exists(db, table_id, &exists, &sort_index, err, err_sz) != 0) return -1;
    if (!exists) {
        if (preserve_sort_index && explicit_sort_index >= 0) {
            sort_index = explicit_sort_index;
        } else if (next_sort_index(db, &sort_index, err, err_sz) != 0) {
            return -1;
        }
    }

    if (exec_sql(db->conn, "BEGIN IMMEDIATE;", err, err_sz) != 0) return -1;

    stmt = NULL;
    rc = sqlite3_prepare_v2(db->conn,
        "INSERT INTO book_tables(id, name, sort_index) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback;
    }
    sqlite3_bind_text(stmt, 1, table_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, table->name ? table->name : "Untitled Table", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, sort_index);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback;
    }

    stmt = NULL;
    rc = sqlite3_prepare_v2(db->conn,
        "DELETE FROM book_columns WHERE table_id = ?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback;
    }
    sqlite3_bind_text(stmt, 1, table_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback;
    }

    {
        char drop_sql[1200];
        char *create_sql = NULL;
        size_t sql_sz = 128 + ((size_t)table->column_count * 48);
        size_t off;

        snprintf(drop_sql, sizeof(drop_sql), "DROP TABLE IF EXISTS %s;", q_storage_name);
        if (exec_sql(db->conn, drop_sql, err, err_sz) != 0) goto rollback;

        create_sql = (char *)malloc(sql_sz);
        if (!create_sql) {
            set_err(err, err_sz, "Out of memory");
            goto rollback;
        }
        off = (size_t)snprintf(create_sql, sql_sz, "CREATE TABLE %s (row_id INTEGER PRIMARY KEY", q_storage_name);
        if (off >= sql_sz) {
            free(create_sql);
            set_err(err, err_sz, "SQL buffer overflow");
            goto rollback;
        }
        for (int i = 0; i < table->column_count; ++i) {
            int written = snprintf(create_sql + off, sql_sz - off, ", c%d TEXT", i);
            if (written < 0 || (size_t)written >= sql_sz - off) {
                free(create_sql);
                set_err(err, err_sz, "SQL buffer overflow");
                goto rollback;
            }
            off += (size_t)written;
        }
        if (off + 3 >= sql_sz) {
            free(create_sql);
            set_err(err, err_sz, "SQL buffer overflow");
            goto rollback;
        }
        strcpy(create_sql + off, ");");
        if (exec_sql(db->conn, create_sql, err, err_sz) != 0) {
            free(create_sql);
            goto rollback;
        }
        free(create_sql);
    }

    rc = sqlite3_prepare_v2(db->conn,
        "INSERT INTO book_columns(table_id, col_index, name, type, color_pair_id) "
        "VALUES(?1, ?2, ?3, ?4, ?5);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback;
    }
    for (int i = 0; i < table->column_count; ++i) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, table_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, i);
        sqlite3_bind_text(stmt, 3, table->columns[i].name ? table->columns[i].name : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, type_to_string(table->columns[i].type), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, table->columns[i].color_pair_id);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            set_err(err, err_sz, sqlite3_errmsg(db->conn));
            sqlite3_finalize(stmt);
            stmt = NULL;
            goto rollback;
        }
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (table->row_count > 0) {
        char *insert_sql = NULL;
        size_t sql_sz = 128 + ((size_t)table->column_count * 16);
        size_t off = 0;

        insert_sql = (char *)malloc(sql_sz);
        if (!insert_sql) {
            set_err(err, err_sz, "Out of memory");
            goto rollback;
        }
        off += (size_t)snprintf(insert_sql + off, sql_sz - off, "INSERT INTO %s (", q_storage_name);
        for (int i = 0; i < table->column_count; ++i) {
            int written = snprintf(insert_sql + off, sql_sz - off, "%sc%d", i ? ", " : "", i);
            if (written < 0 || (size_t)written >= sql_sz - off) {
                free(insert_sql);
                set_err(err, err_sz, "SQL buffer overflow");
                goto rollback;
            }
            off += (size_t)written;
        }
        off += (size_t)snprintf(insert_sql + off, sql_sz - off, ") VALUES (");
        for (int i = 0; i < table->column_count; ++i) {
            int written = snprintf(insert_sql + off, sql_sz - off, "%s?%d", i ? ", " : "", i + 1);
            if (written < 0 || (size_t)written >= sql_sz - off) {
                free(insert_sql);
                set_err(err, err_sz, "SQL buffer overflow");
                goto rollback;
            }
            off += (size_t)written;
        }
        if (off + 3 >= sql_sz) {
            free(insert_sql);
            set_err(err, err_sz, "SQL buffer overflow");
            goto rollback;
        }
        strcpy(insert_sql + off, ");");

        rc = sqlite3_prepare_v2(db->conn, insert_sql, -1, &stmt, NULL);
        free(insert_sql);
        if (rc != SQLITE_OK) {
            set_err(err, err_sz, sqlite3_errmsg(db->conn));
            goto rollback;
        }

        for (int r = 0; r < table->row_count; ++r) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            for (int c = 0; c < table->column_count; ++c) {
                void *val = table->rows[r].values ? table->rows[r].values[c] : NULL;
                if (!val) {
                    sqlite3_bind_null(stmt, c + 1);
                    continue;
                }
                switch (table->columns[c].type) {
                    case TYPE_INT: {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%d", *(int *)val);
                        sqlite3_bind_text(stmt, c + 1, buf, -1, SQLITE_TRANSIENT);
                        break;
                    }
                    case TYPE_FLOAT: {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.9g", (double)*(float *)val);
                        sqlite3_bind_text(stmt, c + 1, buf, -1, SQLITE_TRANSIENT);
                        break;
                    }
                    case TYPE_BOOL:
                        sqlite3_bind_text(stmt, c + 1, (*(int *)val) ? "true" : "false", -1, SQLITE_TRANSIENT);
                        break;
                    case TYPE_STR:
                    default:
                        sqlite3_bind_text(stmt, c + 1, (const char *)val, -1, SQLITE_TRANSIENT);
                        break;
                }
            }
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                set_err(err, err_sz, sqlite3_errmsg(db->conn));
                sqlite3_finalize(stmt);
                stmt = NULL;
                goto rollback;
            }
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (exec_sql(db->conn, "COMMIT;", err, err_sz) != 0) return -1;
    return 0;

rollback:
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_exec(db->conn, "ROLLBACK;", NULL, NULL, NULL);
    return -1;
}

BookDB *bookdb_open(const char *path, int create_if_missing, char *err, size_t err_sz)
{
    BookDB *db;
    int flags = SQLITE_OPEN_READWRITE;

    if (!path || !*path) {
        set_err(err, err_sz, "No path provided");
        return NULL;
    }
    if (create_if_missing) flags |= SQLITE_OPEN_CREATE;

    db = (BookDB *)calloc(1, sizeof(BookDB));
    if (!db) {
        set_err(err, err_sz, "Out of memory");
        return NULL;
    }
    if (sqlite3_open_v2(path, &db->conn, flags, NULL) != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        if (db->conn) sqlite3_close(db->conn);
        free(db);
        return NULL;
    }
    strncpy(db->path, path, sizeof(db->path) - 1);
    db->path[sizeof(db->path) - 1] = '\0';
    return db;
}

void bookdb_close(BookDB *db)
{
    if (!db) return;
    if (db->conn) sqlite3_close(db->conn);
    free(db);
}

int bookdb_init_schema(BookDB *db, char *err, size_t err_sz)
{
    if (!db || !db->conn) {
        set_err(err, err_sz, "Book DB is not open");
        return -1;
    }
    if (exec_sql(db->conn,
            "CREATE TABLE IF NOT EXISTS book_meta ("
            "key TEXT PRIMARY KEY,"
            "value TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS book_tables ("
            "id TEXT PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "sort_index INTEGER NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS book_columns ("
            "table_id TEXT NOT NULL,"
            "col_index INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "type TEXT NOT NULL,"
            "color_pair_id INTEGER NOT NULL,"
            "PRIMARY KEY (table_id, col_index)"
            ");",
            err, err_sz) != 0) {
        return -1;
    }
    if (set_meta(db, "format_version", BOOKDB_FORMAT_VERSION, err, err_sz) != 0) return -1;
    return 0;
}

int bookdb_create_empty(const char *path, const char *book_name, char *err, size_t err_sz)
{
    BookDB *db;

    if (!path || !*path) {
        set_err(err, err_sz, "No book path provided");
        return -1;
    }
    unlink(path);
    db = bookdb_open(path, 1, err, err_sz);
    if (!db) return -1;
    if (bookdb_init_schema(db, err, err_sz) != 0 ||
        bookdb_set_book_name(db, book_name ? book_name : "Untitled Book", err, err_sz) != 0 ||
        bookdb_set_active_table_id(db, "", err, err_sz) != 0) {
        bookdb_close(db);
        return -1;
    }
    bookdb_close(db);
    return 0;
}

int bookdb_get_book_name(BookDB *db, char *out, size_t out_sz, char *err, size_t err_sz)
{
    return get_meta(db, "book_name", out, out_sz, "Untitled Book", err, err_sz);
}

int bookdb_set_book_name(BookDB *db, const char *name, char *err, size_t err_sz)
{
    return set_meta(db, "book_name", (name && *name) ? name : "Untitled Book", err, err_sz);
}

int bookdb_get_active_table_id(BookDB *db, char *out, size_t out_sz, char *err, size_t err_sz)
{
    return get_meta(db, "active_table_id", out, out_sz, "", err, err_sz);
}

int bookdb_set_active_table_id(BookDB *db, const char *table_id, char *err, size_t err_sz)
{
    return set_meta(db, "active_table_id", table_id ? table_id : "", err, err_sz);
}

int bookdb_list_tables(BookDB *db, char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int cap = 8;
    int count = 0;
    char **names = NULL;
    char **ids = NULL;
    int rc;

    if (names_out) *names_out = NULL;
    if (ids_out) *ids_out = NULL;
    if (count_out) *count_out = 0;
    if (!db || !db->conn) {
        set_err(err, err_sz, "Book DB is not open");
        return -1;
    }

    names = (char **)calloc(cap, sizeof(char *));
    ids = (char **)calloc(cap, sizeof(char *));
    if (!names || !ids) {
        free(names);
        free(ids);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT name, id FROM book_tables ORDER BY sort_index, name;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(names);
        free(ids);
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name_txt = sqlite3_column_text(stmt, 0);
        const unsigned char *id_txt = sqlite3_column_text(stmt, 1);
        if (count == cap) {
            char **new_names;
            char **new_ids;
            cap *= 2;
            new_names = (char **)realloc(names, sizeof(char *) * cap);
            new_ids = (char **)realloc(ids, sizeof(char *) * cap);
            if (!new_names || !new_ids) {
                free(new_names);
                free(new_ids);
                rc = SQLITE_NOMEM;
                break;
            }
            names = new_names;
            ids = new_ids;
        }
        names[count] = strdup(name_txt ? (const char *)name_txt : "");
        ids[count] = strdup(id_txt ? (const char *)id_txt : "");
        if (!names[count] || !ids[count]) {
            rc = SQLITE_NOMEM;
            break;
        }
        count++;
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        for (int i = 0; i < count; ++i) {
            free(names[i]);
            free(ids[i]);
        }
        free(names);
        free(ids);
        set_err(err, err_sz, rc == SQLITE_NOMEM ? "Out of memory" : sqlite3_errmsg(db->conn));
        return -1;
    }

    if (names_out) *names_out = names; else {
        for (int i = 0; i < count; ++i) free(names[i]);
        free(names);
    }
    if (ids_out) *ids_out = ids; else {
        for (int i = 0; i < count; ++i) free(ids[i]);
        free(ids);
    }
    if (count_out) *count_out = count;
    return 0;
}

int bookdb_save_table(BookDB *db, const char *table_id, const Table *table, char *err, size_t err_sz)
{
    char generated_id[256];
    const char *effective_id = table_id;

    if (!db || !db->conn || !table) {
        set_err(err, err_sz, "Invalid args");
        return -1;
    }
    if (!effective_id || !*effective_id) {
        if (bookdb_create_table(db, table, generated_id, sizeof(generated_id), err, err_sz) != 0) return -1;
        return bookdb_set_active_table_id(db, generated_id, err, err_sz);
    }
    return save_table_with_id(db, effective_id, table, 0, -1, err, err_sz);
}

Table *bookdb_load_table(BookDB *db, const char *table_id, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    Table *table = NULL;
    char storage_name[512];
    char q_storage_name[1024];
    char *select_sql = NULL;
    int column_count = 0;
    int rc;
    const char *load_id = table_id;
    char active_id[256];

    if (!db || !db->conn) {
        set_err(err, err_sz, "Book DB is not open");
        return NULL;
    }
    if (!load_id || !*load_id) {
        if (bookdb_get_active_table_id(db, active_id, sizeof(active_id), err, err_sz) != 0) return NULL;
        load_id = active_id;
    }
    if (!load_id || !*load_id) {
        set_err(err, err_sz, "No table selected");
        return NULL;
    }

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT name FROM book_tables WHERE id = ?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, load_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        set_err(err, err_sz, "Book table not found");
        return NULL;
    }
    table = create_table((const char *)sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (!table) {
        set_err(err, err_sz, "Out of memory");
        return NULL;
    }

    rc = sqlite3_prepare_v2(db->conn,
        "SELECT name, type, color_pair_id FROM book_columns "
        "WHERE table_id = ?1 ORDER BY col_index;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        free_table(table);
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, load_id, -1, SQLITE_TRANSIENT);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name_txt = sqlite3_column_text(stmt, 0);
        const unsigned char *type_txt = sqlite3_column_text(stmt, 1);
        DataType type = parse_type_from_string(type_txt ? (const char *)type_txt : "str");
        if (type == TYPE_UNKNOWN) type = TYPE_STR;
        add_column(table, name_txt ? (const char *)name_txt : "", type);
        table->columns[column_count].color_pair_id = sqlite3_column_int(stmt, 2);
        column_count++;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        free_table(table);
        return NULL;
    }
    if (column_count == 0) {
        table->dirty = 0;
        return table;
    }

    if (storage_table_name(load_id, storage_name, sizeof(storage_name)) != 0) {
        set_err(err, err_sz, "Table id too long");
        free_table(table);
        return NULL;
    }
    quote_ident(q_storage_name, sizeof(q_storage_name), storage_name);

    {
        size_t sql_sz = 128 + ((size_t)column_count * 16);
        size_t off = 0;
        select_sql = (char *)malloc(sql_sz);
        if (!select_sql) {
            set_err(err, err_sz, "Out of memory");
            free_table(table);
            return NULL;
        }
        off += (size_t)snprintf(select_sql + off, sql_sz - off, "SELECT ");
        for (int i = 0; i < column_count; ++i) {
            int written = snprintf(select_sql + off, sql_sz - off, "%sc%d", i ? ", " : "", i);
            if (written < 0 || (size_t)written >= sql_sz - off) {
                free(select_sql);
                set_err(err, err_sz, "SQL buffer overflow");
                free_table(table);
                return NULL;
            }
            off += (size_t)written;
        }
        snprintf(select_sql + off, sql_sz - off, " FROM %s ORDER BY row_id;", q_storage_name);
    }

    rc = sqlite3_prepare_v2(db->conn, select_sql, -1, &stmt, NULL);
    free(select_sql);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        free_table(table);
        return NULL;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        char **vals = NULL;

        if (column_count > 0) {
            vals = (char **)calloc((size_t)column_count, sizeof(char *));
            if (!vals) {
                sqlite3_finalize(stmt);
                free_table(table);
                set_err(err, err_sz, "Out of memory");
                return NULL;
            }
        }
        for (int c = 0; c < column_count; ++c) {
            const unsigned char *txt = sqlite3_column_text(stmt, c);
            vals[c] = strdup(txt ? (const char *)txt : "");
            if (!vals[c]) {
                for (int i = 0; i < c; ++i) free(vals[i]);
                free(vals);
                sqlite3_finalize(stmt);
                free_table(table);
                set_err(err, err_sz, "Out of memory");
                return NULL;
            }
        }
        add_row(table, (const char **)vals);
        for (int c = 0; c < column_count; ++c) free(vals[c]);
        free(vals);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        free_table(table);
        return NULL;
    }
    table->dirty = 0;
    return table;
}

int bookdb_create_table(BookDB *db, const Table *table, char *out_table_id, size_t out_sz, char *err, size_t err_sz)
{
    char *base = NULL;
    char candidate[256];
    int suffix = 1;
    int exists = 0;

    if (out_table_id && out_sz > 0) out_table_id[0] = '\0';
    if (!db || !db->conn || !table) {
        set_err(err, err_sz, "Invalid args");
        return -1;
    }

    base = sanitize_component(table->name && *table->name ? table->name : "table");
    if (!base) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    do {
        if (suffix == 1) snprintf(candidate, sizeof(candidate), "%s", base);
        else snprintf(candidate, sizeof(candidate), "%s_%d", base, suffix);
        if (table_exists(db, candidate, &exists, NULL, err, err_sz) != 0) {
            free(base);
            return -1;
        }
        suffix++;
    } while (exists);

    free(base);
    if (save_table_with_id(db, candidate, table, 0, -1, err, err_sz) != 0) return -1;
    if (out_table_id && out_sz > 0) {
        strncpy(out_table_id, candidate, out_sz - 1);
        out_table_id[out_sz - 1] = '\0';
    }
    return 0;
}

int bookdb_delete_table(BookDB *db, const char *table_id, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    char storage_name[512];
    char q_storage_name[1024];
    char drop_sql[1200];
    int exists = 0;
    int sort_index = -1;
    int rc;

    if (!db || !db->conn || !table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    if (table_exists(db, table_id, &exists, &sort_index, err, err_sz) != 0) return -1;
    if (!exists) {
        set_err(err, err_sz, "Book table not found");
        return -1;
    }
    if (storage_table_name(table_id, storage_name, sizeof(storage_name)) != 0) {
        set_err(err, err_sz, "Table id too long");
        return -1;
    }
    quote_ident(q_storage_name, sizeof(q_storage_name), storage_name);

    if (exec_sql(db->conn, "BEGIN IMMEDIATE;", err, err_sz) != 0) return -1;

    rc = sqlite3_prepare_v2(db->conn, "DELETE FROM book_columns WHERE table_id = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }
    sqlite3_bind_text(stmt, 1, table_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }

    rc = sqlite3_prepare_v2(db->conn, "DELETE FROM book_tables WHERE id = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }
    sqlite3_bind_text(stmt, 1, table_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }

    rc = sqlite3_prepare_v2(db->conn,
        "UPDATE book_tables SET sort_index = sort_index - 1 WHERE sort_index > ?1;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }
    sqlite3_bind_int(stmt, 1, sort_index);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        goto rollback_delete;
    }

    snprintf(drop_sql, sizeof(drop_sql), "DROP TABLE IF EXISTS %s;", q_storage_name);
    if (exec_sql(db->conn, drop_sql, err, err_sz) != 0) goto rollback_delete;

    if (exec_sql(db->conn, "COMMIT;", err, err_sz) != 0) return -1;
    return 0;

rollback_delete:
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_exec(db->conn, "ROLLBACK;", NULL, NULL, NULL);
    return -1;
}

int bookdb_rename_table(BookDB *db, const char *table_id, const char *name, char *err, size_t err_sz)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (!db || !db->conn || !table_id || !*table_id) {
        set_err(err, err_sz, "No table selected");
        return -1;
    }
    rc = sqlite3_prepare_v2(db->conn,
        "UPDATE book_tables SET name = ?1 WHERE id = ?2;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, (name && *name) ? name : "Untitled Table", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, table_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        set_err(err, err_sz, sqlite3_errmsg(db->conn));
        return -1;
    }
    if (sqlite3_changes(db->conn) == 0) {
        set_err(err, err_sz, "Book table not found");
        return -1;
    }
    return 0;
}

int bookdb_export_legacy_ttbx(BookDB *db, const char *dest_path, char *err, size_t err_sz)
{
    char **names = NULL;
    char **ids = NULL;
    int count = 0;
    TtbxManifest manifest;
    char book_name[256];
    char active_table_id[256];

    if (!db || !db->conn || !dest_path || !*dest_path) {
        set_err(err, err_sz, "Invalid export path");
        return -1;
    }
    if (bookdb_list_tables(db, &names, &ids, &count, err, err_sz) != 0) return -1;
    if (count <= 0) {
        set_err(err, err_sz, "Book has no tables");
        free(names);
        free(ids);
        return -1;
    }
    ttbx_remove_book(dest_path, NULL, 0);

    for (int i = 0; i < count; ++i) {
        Table *table = bookdb_load_table(db, ids[i], err, err_sz);
        if (!table) {
            for (int j = 0; j < count; ++j) {
                free(names[j]);
                free(ids[j]);
            }
            free(names);
            free(ids);
            return -1;
        }
        if (ttbx_save_table(table, dest_path, NULL, err, err_sz) != 0) {
            free_table(table);
            for (int j = 0; j < count; ++j) {
                free(names[j]);
                free(ids[j]);
            }
            free(names);
            free(ids);
            return -1;
        }
        free_table(table);
    }

    if (ttbx_manifest_load(dest_path, &manifest, err, err_sz) != 0) {
        for (int j = 0; j < count; ++j) {
            free(names[j]);
            free(ids[j]);
        }
        free(names);
        free(ids);
        return -1;
    }
    if (bookdb_get_book_name(db, book_name, sizeof(book_name), err, err_sz) != 0 ||
        bookdb_get_active_table_id(db, active_table_id, sizeof(active_table_id), err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        for (int j = 0; j < count; ++j) {
            free(names[j]);
            free(ids[j]);
        }
        free(names);
        free(ids);
        return -1;
    }

    free(manifest.book_name);
    manifest.book_name = strdup(book_name);
    free(manifest.active_table_id);
    manifest.active_table_id = strdup(active_table_id);
    if (!manifest.book_name || !manifest.active_table_id || manifest.table_count != count) {
        ttbx_manifest_free(&manifest);
        for (int j = 0; j < count; ++j) {
            free(names[j]);
            free(ids[j]);
        }
        free(names);
        free(ids);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        free(manifest.tables[i].id);
        free(manifest.tables[i].name);
        manifest.tables[i].id = strdup(ids[i]);
        manifest.tables[i].name = strdup(names[i]);
        if (!manifest.tables[i].id || !manifest.tables[i].name) {
            ttbx_manifest_free(&manifest);
            for (int j = 0; j < count; ++j) {
                free(names[j]);
                free(ids[j]);
            }
            free(names);
            free(ids);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }
    if (ttbx_manifest_save(dest_path, &manifest, err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        for (int j = 0; j < count; ++j) {
            free(names[j]);
            free(ids[j]);
        }
        free(names);
        free(ids);
        return -1;
    }
    ttbx_manifest_free(&manifest);

    for (int i = 0; i < count; ++i) {
        free(names[i]);
        free(ids[i]);
    }
    free(names);
    free(ids);
    return 0;
}

int bookdb_import_legacy_ttbx(const char *legacy_path, const char *dest_db_path, char *err, size_t err_sz)
{
    TtbxManifest manifest;
    BookDB *db = NULL;

    if (!legacy_path || !*legacy_path || !dest_db_path || !*dest_db_path) {
        set_err(err, err_sz, "Invalid import path");
        return -1;
    }
    if (ttbx_manifest_load(legacy_path, &manifest, err, err_sz) != 0) return -1;
    if (bookdb_create_empty(dest_db_path, manifest.book_name ? manifest.book_name : "Untitled Book", err, err_sz) != 0) {
        ttbx_manifest_free(&manifest);
        return -1;
    }
    db = bookdb_open(dest_db_path, 0, err, err_sz);
    if (!db) {
        ttbx_manifest_free(&manifest);
        return -1;
    }
    if (bookdb_init_schema(db, err, err_sz) != 0) {
        bookdb_close(db);
        ttbx_manifest_free(&manifest);
        return -1;
    }

    for (int i = 0; i < manifest.table_count; ++i) {
        Table *table = ttbx_load_table(legacy_path, manifest.tables[i].id, err, err_sz);
        if (!table) {
            bookdb_close(db);
            ttbx_manifest_free(&manifest);
            return -1;
        }
        if (save_table_with_id(db, manifest.tables[i].id, table, 1, i, err, err_sz) != 0) {
            free_table(table);
            bookdb_close(db);
            ttbx_manifest_free(&manifest);
            return -1;
        }
        free_table(table);
    }
    if (bookdb_set_active_table_id(db, manifest.active_table_id ? manifest.active_table_id : "", err, err_sz) != 0) {
        bookdb_close(db);
        ttbx_manifest_free(&manifest);
        return -1;
    }

    bookdb_close(db);
    ttbx_manifest_free(&manifest);
    return 0;
}
