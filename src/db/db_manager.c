#include "db_manager.h"
#include "errors.h"
#include "tablecraft.h"
#include "workspace.h"

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

struct DbManager {
    sqlite3 *conn;
    char     path[512];
};

// Active connection (legacy DB support)
static DbManager *ACTIVE_DB = NULL;

void db_set_active(DbManager *db) { ACTIVE_DB = db; }
DbManager *db_get_active(void) { return ACTIVE_DB; }
int db_autosave_enabled(void) { return workspace_autosave_enabled(); }
void db_set_autosave_enabled(int enabled) { workspace_set_autosave_enabled(enabled); }

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (!err || err_sz == 0) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int path_exists(const char *p) {
    struct stat st; return stat(p, &st) == 0;
}

static void join_path(char *out, size_t out_sz, const char *a, const char *b) {
    size_t la = strlen(a);
    int needs_sep = (la > 0 && a[la - 1] != '/');
    snprintf(out, out_sz, needs_sep ? "%s/%s" : "%s%s", a, b);
}

// (search helpers removed; UI implements search mode)

// Forwards for helpers used below
static void quote_ident(char *out, size_t out_sz, const char *name);
static int fetch_columns(DbManager *db, const char *table, char ***names, char ***types, int *count);

int db_ensure_databases_dir(char *err, size_t err_sz) {
    char path[512];
    // cwd + "/databases"
    if (!getcwd(path, sizeof(path))) {
        set_err(err, err_sz, "Failed to get CWD");
        return -1;
    }
    size_t len = strlen(path);
    if (len + 11 >= sizeof(path)) {
        set_err(err, err_sz, "Path too long");
        return -1;
    }
    strcat(path, "/databases");
    if (path_exists(path)) return 0;
    if (mkdir(path, 0755) != 0) {
        set_err(err, err_sz, "Failed to create databases directory");
        return -1;
    }
    return 0;
}

static void databases_dir(char *out, size_t out_sz) {
    char cwd[512];
    getcwd(cwd, sizeof(cwd));
    join_path(out, out_sz, cwd, "databases");
}

int db_create_database(const char *name, char *err, size_t err_sz) {
    char dir[512]; databases_dir(dir, sizeof(dir));
    if (db_ensure_databases_dir(err, err_sz) != 0) return -1;
    char path[1024]; join_path(path, sizeof(path), dir, name);
    // Create empty file
    FILE *f = fopen(path, "wb");
    if (!f) { set_err(err, err_sz, "Failed to create database file"); return -1; }
    fclose(f);
    // Optionally open to initialize SQLite header
    sqlite3 *tmp = NULL;
    if (sqlite3_open(path, &tmp) != SQLITE_OK) {
        set_err(err, err_sz, "SQLite open failed");
        if (tmp) sqlite3_close(tmp);
        return -1;
    }
    sqlite3_close(tmp);
    return 0;
}

int db_delete_database(const char *name, char *err, size_t err_sz) {
    char dir[512]; databases_dir(dir, sizeof(dir));
    char path[1024]; join_path(path, sizeof(path), dir, name);
    if (!path_exists(path)) { set_err(err, err_sz, "Database does not exist"); return -1; }
    if (remove(path) != 0) { set_err(err, err_sz, "Failed to delete database"); return -1; }
    return 0;
}

int db_list_databases(char ***names_out, int *count_out, char *err, size_t err_sz) {
    if (!names_out || !count_out) { set_err(err, err_sz, "Invalid args"); return -1; }
    *names_out = NULL; *count_out = 0;
    char dir[512]; databases_dir(dir, sizeof(dir));
    if (db_ensure_databases_dir(err, err_sz) != 0) return -1;
    DIR *d = opendir(dir);
    if (!d) { set_err(err, err_sz, "Failed to open databases directory"); return -1; }
    struct dirent *de;
    int cap = 8; int n = 0;
    char **list = (char**)malloc(sizeof(char*) * cap);
    while ((de = readdir(d))) {
        const char *nm = de->d_name;
        size_t ln = strlen(nm);
        if (ln >= 3 && strcmp(nm, ".") && strcmp(nm, "..")) {
            // crude filter: ends with .db
            if (ln >= 3 && ln >= 3 && ln >= 3) {}
            if (ln >= 3 && strcmp(nm + ln - 3, ".db") == 0) {
                if (n == cap) { cap *= 2; list = (char**)realloc(list, sizeof(char*) * cap); }
                list[n++] = strdup(nm);
            }
        }
    }
    closedir(d);
    *names_out = list; *count_out = n;
    return 0;
}

DbManager *db_open(const char *path, char *err, size_t err_sz) {
    if (!path || !*path) { set_err(err, err_sz, "No path provided"); return NULL; }
    if (!path_exists(path)) { set_err(err, err_sz, "Database does not exist"); return NULL; }
    sqlite3 *conn = NULL;
    if (sqlite3_open(path, &conn) != SQLITE_OK) {
        set_err(err, err_sz, "Failed to open database");
        if (conn) sqlite3_close(conn);
        return NULL;
    }
    DbManager *db = (DbManager*)calloc(1, sizeof(DbManager));
    db->conn = conn;
    strncpy(db->path, path, sizeof(db->path)-1);
    return db;
}

void db_close(DbManager *db) {
    if (!db) return;
    if (db->conn) sqlite3_close(db->conn);
    free(db);
}

int db_is_connected(DbManager *db) { return db && db->conn; }
const char *db_current_path(DbManager *db) { return db ? db->path : NULL; }

int db_list_tables(DbManager *db, char ***names_out, int *count_out, char *err, size_t err_sz) {
    if (!db || !db->conn) { set_err(err, err_sz, "Not connected"); return -1; }
    if (!names_out || !count_out) { set_err(err, err_sz, "Invalid args"); return -1; }
    *names_out = NULL; *count_out = 0;

    const char *sql = "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, err_sz, "Query failed");
        return -1;
    }
    int cap = 8, n = 0; char **list = (char**)malloc(sizeof(char*) * cap);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        if (!txt) continue;
        if (n == cap) { cap *= 2; list = (char**)realloc(list, sizeof(char*) * cap); }
        list[n++] = strdup((const char*)txt);
    }
    sqlite3_finalize(st);
    *names_out = list; *count_out = n;
    return 0;
}

int db_table_exists(DbManager *db, const char *name) {
    if (!db || !db->conn || !name || !*name) return 0;
    char **names = NULL; int count = 0; char err[64] = {0};
    if (db_list_tables(db, &names, &count, err, sizeof(err)) != 0) return 0;
    int found = 0;
    for (int i = 0; i < count; ++i) {
        if (strcmp(names[i], name) == 0) { found = 1; }
        free(names[i]);
    }
    free(names);
    return found;
}

int db_delete_table(DbManager *db, const char *name, char *err, size_t err_sz) {
    if (!db || !db->conn) { set_err(err, err_sz, "Not connected"); return -1; }
    if (!name || !*name) { set_err(err, err_sz, "No table name"); return -1; }
    char qname[512]; quote_ident(qname, sizeof(qname), name);
    char sql[1024]; snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS %s;", qname);
    char *errmsg = NULL;
    if (sqlite3_exec(db->conn, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        set_err(err, err_sz, errmsg ? errmsg : "DROP failed");
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    if (errmsg) sqlite3_free(errmsg);
    return 0;
}

Table* db_load_table(DbManager *db, const char *name, char *err, size_t err_sz) {
    if (!db || !db->conn) { set_err(err, err_sz, "Not connected"); return NULL; }
    if (!name || !*name) { set_err(err, err_sz, "No table name"); return NULL; }

    // Fetch columns
    char **col_names = NULL; char **col_types = NULL; int col_count = 0;
    if (fetch_columns(db, name, &col_names, &col_types, &col_count) != 0 || col_count == 0) {
        set_err(err, err_sz, "No columns");
        return NULL;
    }

    Table *t = create_table(name);
    for (int j = 0; j < col_count; ++j) {
        DataType dt = parse_type_from_string(col_types[j] && *col_types[j] ? col_types[j] : "str");
        if (dt == TYPE_UNKNOWN) dt = TYPE_STR;
        add_column(t, col_names[j], dt);
    }

    // Read all rows
    char sql[512]; snprintf(sql, sizeof(sql), "SELECT * FROM \"%s\";", name);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, err_sz, "SELECT failed");
        for (int i = 0; i < col_count; ++i) { free(col_names[i]); free(col_types[i]); }
        free(col_names); free(col_types);
        free_table(t);
        return NULL;
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        // Build input_strings array for add_row
        char **vals = (char**)malloc(sizeof(char*) * col_count);
        for (int c = 0; c < col_count; ++c) {
            const unsigned char *txt = sqlite3_column_text(st, c);
            if (txt) vals[c] = strdup((const char*)txt);
            else vals[c] = strdup("");
        }
        add_row(t, (const char**)vals);
        for (int c = 0; c < col_count; ++c) free(vals[c]);
        free(vals);
    }
    sqlite3_finalize(st);

    for (int i = 0; i < col_count; ++i) { free(col_names[i]); free(col_types[i]); }
    free(col_names); free(col_types);
    return t;
}

static int fetch_columns(DbManager *db, const char *table, char ***names, char ***types, int *count) {
    *names = NULL; *types = NULL; *count = 0;
    char sql[512]; snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\");", table);
    sqlite3_stmt *st = NULL; if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int cap = 8, n = 0; char **nn = (char**)malloc(sizeof(char*)*cap); char **tt = (char**)malloc(sizeof(char*)*cap);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(st, 1);
        const char *type = (const char*)sqlite3_column_text(st, 2);
        if (!name) name = "";
        if (!type) type = "";
        if (n == cap) { cap *= 2; nn = (char**)realloc(nn, sizeof(char*)*cap); tt = (char**)realloc(tt, sizeof(char*)*cap); }
        nn[n] = strdup(name); tt[n] = strdup(type); n++;
    }
    sqlite3_finalize(st);
    *names = nn; *types = tt; *count = n;
    return 0;
}

// (db_search removed)

// Helpers for saving Table to SQLite
static const char *map_dtype(DataType t) {
    switch (t) {
        case TYPE_INT: return "INTEGER";
        case TYPE_FLOAT: return "REAL";
        case TYPE_BOOL: return "INTEGER";
        case TYPE_STR: return "TEXT";
        default: return "TEXT";
    }
}

static void quote_ident(char *out, size_t out_sz, const char *name) {
    // Double any quotes and wrap with quotes
    size_t n = 0; out[n++] = '"';
    for (const char *p = name; *p && n < out_sz - 2; ++p) {
        if (*p == '"') { if (n < out_sz - 2) out[n++] = '"'; }
        out[n++] = *p;
        if (n >= out_sz - 1) break;
    }
    out[n++] = '"'; out[n] = '\0';
}

int db_save_table(DbManager *db, const Table *t, char *err, size_t err_sz) {
    if (!db || !db->conn) { set_err(err, err_sz, "Not connected"); return -1; }
    if (!t) { set_err(err, err_sz, "No table"); return -1; }

    // Begin transaction
    char *errmsg = NULL;
    if (sqlite3_exec(db->conn, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK) {
        set_err(err, err_sz, errmsg ? errmsg : "BEGIN failed");
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }

    int rc = 0;
    char qname[512]; quote_ident(qname, sizeof(qname), t->name);

    // Drop existing table
    char sql[1024];
    snprintf(sql, sizeof(sql), "DROP TABLE IF EXISTS %s;", qname);
    if (sqlite3_exec(db->conn, sql, NULL, NULL, &errmsg) != SQLITE_OK) { rc = -1; }
    if (errmsg) { sqlite3_free(errmsg); errmsg = NULL; }

    if (rc == 0) {
        // Create table
        size_t off = 0; off += snprintf(sql + off, sizeof(sql) - off, "CREATE TABLE %s (", qname);
        for (int j = 0; j < t->column_count; ++j) {
            char qc[256]; quote_ident(qc, sizeof(qc), t->columns[j].name);
            off += snprintf(sql + off, sizeof(sql) - off, "%s %s%s",
                            qc, map_dtype(t->columns[j].type), (j < t->column_count - 1) ? "," : ")");
            if (off >= sizeof(sql)) { rc = -1; break; }
        }
        if (rc == 0 && sqlite3_exec(db->conn, sql, NULL, NULL, &errmsg) != SQLITE_OK) rc = -1;
        if (errmsg) { sqlite3_free(errmsg); errmsg = NULL; }
    }

    if (rc == 0 && t->row_count > 0 && t->column_count > 0) {
        // Prepare insert statement with parameters
        size_t off = 0;
        int n = snprintf(sql + off, sizeof(sql) - off, "INSERT INTO %s VALUES (", qname);
        if (n < 0 || (size_t)n >= sizeof(sql) - off) goto sql_overflow;
        off += n;
        for (int j = 0; j < t->column_count; ++j) {
            n = snprintf(sql + off, sizeof(sql) - off, "%s", (j ? ",?" : "?"));
            if (n < 0 || (size_t)n >= sizeof(sql) - off) goto sql_overflow;
            off += n;
        }
        n = snprintf(sql + off, sizeof(sql) - off, ");");
        if (n < 0 || (size_t)n >= sizeof(sql) - off) goto sql_overflow;
        off += n;
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db->conn, sql, -1, &st, NULL) != SQLITE_OK) rc = -1;
        if (rc == 0) {
            for (int i = 0; i < t->row_count; ++i) {
                sqlite3_reset(st);
                for (int j = 0; j < t->column_count; ++j) {
                    void *v = (t->rows[i].values ? t->rows[i].values[j] : NULL);
                    DataType ty = t->columns[j].type;
                    int idx = j + 1;
                    if (!v) { sqlite3_bind_null(st, idx); continue; }
                    switch (ty) {
                        case TYPE_INT:   sqlite3_bind_int(st, idx, *(int*)v); break;
                        case TYPE_FLOAT: sqlite3_bind_double(st, idx, (double)(*(float*)v)); break;
                        case TYPE_BOOL:  sqlite3_bind_int(st, idx, (*(int*)v) ? 1 : 0); break;
                        case TYPE_STR:   sqlite3_bind_text(st, idx, (const char*)v, -1, SQLITE_TRANSIENT); break;
                        default:         sqlite3_bind_null(st, idx); break;
                    }
                }
                if (sqlite3_step(st) != SQLITE_DONE) { rc = -1; break; }
            }
            sqlite3_finalize(st);
        }
sql_overflow:
    rc = -1;
    set_err(err, err_sz, "SQL buffer overflow");
    }

    if (rc == 0) {
        if (sqlite3_exec(db->conn, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
            rc = -1;
            set_err(err, err_sz, errmsg ? errmsg : "COMMIT failed");
        }
    } else {
        sqlite3_exec(db->conn, "ROLLBACK;", NULL, NULL, NULL);
        set_err(err, err_sz, "Save failed");
    }
    if (errmsg) sqlite3_free(errmsg);
    return rc;
}

int db_autosave_table(const Table *t, char *err, size_t err_sz) {
    (void)ACTIVE_DB; // autosave now handled via workspace files
    return workspace_autosave(t, err, err_sz);
}

/* end */
