#define _GNU_SOURCE
#include "seekdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* --------------------------
   Internal state & helpers
   -------------------------- */

struct seekdb {
    sqlite3 *db;
    char    *tmpdir;     /* NULL if connected mode */
    char    *view_name;  /* defaults to _ttb_view */
    char    *key_name;   /* defaults to _ttb_id */
};

static void set_err(char *err, size_t errlen, const char *msg) {
    if (err && errlen) {
        snprintf(err, errlen, "%s", msg ? msg : "error");
    }
}

static int is_sqlite_file(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char hdr[16] = {0};
    size_t n = fread(hdr, 1, 16, f);
    fclose(f);
    return n == 16 && memcmp(hdr, "SQLite format 3\000", 16) == 0;
}

static int exec_sql(sqlite3 *db, const char *sql, char *err, size_t errlen) {
    char *emsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &emsg);
    if (rc != SQLITE_OK) {
        if (emsg) { set_err(err, errlen, emsg); sqlite3_free(emsg); }
        else { set_err(err, errlen, "sqlite exec failed"); }
    }
    return rc;
}

static int apply_pragmas(sqlite3 *db, seekdb_mode_t mode) {
    /* Low-RAM friendly defaults; can be tuned later via a settings screen. */
    int cache_kib = 16384; /* ~16 MiB */
    if (mode == SEEKDB_MODE_LOW_RAM) cache_kib = 8192;
    if (mode == SEEKDB_MODE_NORMAL)  cache_kib = 32768;

    char sql[256];
    snprintf(sql, sizeof(sql),
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA temp_store=FILE;"
        "PRAGMA cache_spill=ON;"
        "PRAGMA cache_size=-%d;", cache_kib);

    return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static int append_key_if_missing(char *dst, size_t dstlen, const char *order_sql, const char *key) {
    if (order_sql && strstr(order_sql, key)) {
        snprintf(dst, dstlen, "%s", order_sql);
    } else if (order_sql && *order_sql) {
        snprintf(dst, dstlen, "%s, %s", order_sql, key);
    } else {
        snprintf(dst, dstlen, "%s", key);
    }
    return 0;
}

/* --------------------------
   Public API (scaffold)
   -------------------------- */

seekdb* seekdb_open(const char *path_or_null, seekdb_mode_t mode, char *err, size_t errlen) {
    seekdb *s = calloc(1, sizeof(*s));
    if (!s) { set_err(err, errlen, "oom"); return NULL; }

    int rc;
    if (path_or_null && is_sqlite_file(path_or_null)) {
        rc = sqlite3_open(path_or_null, &s->db);
    } else {
        /* Use repo-local build/ for ephemeral spill, per project guidelines. */
        (void)mkdir("build", 0755);
        char tmpdir[] = "build/seekdbXXXXXX";
        if (!mkdtemp(tmpdir)) { set_err(err, errlen, "mkdtemp failed"); free(s); return NULL; }
        s->tmpdir = strdup(tmpdir);
        char path[512]; snprintf(path, sizeof(path), "%s/spill.db", tmpdir);
        rc = sqlite3_open(path, &s->db);
    }
    if (rc != SQLITE_OK) {
        set_err(err, errlen, sqlite3_errmsg(s->db));
        sqlite3_close(s->db); free(s->tmpdir); free(s);
        return NULL;
    }
    if (apply_pragmas(s->db, mode) != SQLITE_OK) {
        set_err(err, errlen, "apply pragmas failed");
        sqlite3_close(s->db); free(s->tmpdir); free(s);
        return NULL;
    }

    s->view_name = strdup("_ttb_view");
    s->key_name  = strdup("_ttb_id");
    return s;
}

int seekdb_ensure_table(seekdb *s, const char *table, const char *columns_sql, char *err, size_t errlen) {
    char sql[2048];
    snprintf(sql, sizeof(sql), "CREATE TABLE IF NOT EXISTS \"%s\" (%s);", table, columns_sql);
    return exec_sql(s->db, sql, err, errlen) == SQLITE_OK ? 0 : -1;
}

int seekdb_ensure_stable_key(seekdb *s, const char *table, const char *key, char *err, size_t errlen) {
    free(s->key_name); s->key_name = strdup(key);

    char sql[256];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\");", table);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, errlen, sqlite3_errmsg(s->db)); return -1;
    }
    int has_key = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(st, 1);
        if (name && strcmp((const char*)name, key) == 0) { has_key = 1; break; }
    }
    sqlite3_finalize(st);

    if (!has_key) {
        char buf[512];
        snprintf(buf, sizeof(buf), "ALTER TABLE \"%s\" ADD COLUMN \"%s\" INTEGER;", table, key);
        if (exec_sql(s->db, buf, err, errlen) != SQLITE_OK) return -1;

        snprintf(buf, sizeof(buf), "UPDATE \"%s\" SET \"%s\"=rowid WHERE \"%s\" IS NULL;", table, key, key);
        if (exec_sql(s->db, buf, err, errlen) != SQLITE_OK) return -1;

        snprintf(buf, sizeof(buf), "CREATE UNIQUE INDEX IF NOT EXISTS \"idx_%s_%s\" ON \"%s\"(\"%s\");",
                 table, key, table, key);
        if (exec_sql(s->db, buf, err, errlen) != SQLITE_OK) return -1;
    }
    return 0;
}

int seekdb_set_view(seekdb *s, const char *base_table, const char *where_sql, const char *order_sql,
                    const char *key, char *err, size_t errlen)
{
    if (key && strcmp(key, s->key_name) != 0) { free(s->key_name); s->key_name = strdup(key); }

    char ord[1024];
    append_key_if_missing(ord, sizeof(ord), order_sql, s->key_name);

    char sql[2048];
    snprintf(sql, sizeof(sql), "DROP VIEW IF EXISTS \"%s\";", s->view_name);
    if (exec_sql(s->db, sql, err, errlen) != SQLITE_OK) return -1;

    snprintf(sql, sizeof(sql),
        "CREATE TEMP VIEW \"%s\" AS SELECT * FROM \"%s\" WHERE %s ORDER BY %s;",
        s->view_name, base_table, (where_sql && *where_sql) ? where_sql : "1=1", ord);

    return exec_sql(s->db, sql, err, errlen) == SQLITE_OK ? 0 : -1;
}

static int run_window(seekdb *s, const char *sql, seekdb_row_cb cb, void *user, char *err, size_t errlen) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, errlen, sqlite3_errmsg(s->db)); return -1;
    }
    int delivered = 0;
    for (;;) {
        int rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            ++delivered;
            if (!cb(user, st)) break; /* early stop */
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            set_err(err, errlen, sqlite3_errmsg(s->db));
            delivered = -1; break;
        }
    }
    sqlite3_finalize(st);
    return delivered;
}

int seekdb_seek_first(seekdb *s, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen) {
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT * FROM \"%s\" LIMIT %d;", s->view_name, limit);
    return run_window(s, sql, cb, user, err, errlen);
}

int seekdb_seek_after(seekdb *s, long long last_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT * FROM \"%s\" WHERE \"%s\" > %lld ORDER BY \"%s\" LIMIT %d;",
        s->view_name, s->key_name, last_id, s->key_name, limit);
    return run_window(s, sql, cb, user, err, errlen);
}

int seekdb_seek_before(seekdb *s, long long first_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT * FROM \"%s\" WHERE \"%s\" < %lld ORDER BY \"%s\" DESC LIMIT %d;",
        s->view_name, s->key_name, first_id, s->key_name, limit);
    return run_window(s, sql, cb, user, err, errlen);
}

int seekdb_seek_by_id(seekdb *s, long long target_id, int limit, seekdb_row_cb cb, void *user, char *err, size_t errlen) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT * FROM \"%s\" WHERE \"%s\" >= %lld ORDER BY \"%s\" LIMIT %d;",
        s->view_name, s->key_name, target_id, s->key_name, limit);
    return run_window(s, sql, cb, user, err, errlen);
}

long long seekdb_count(seekdb *s, char *err, size_t errlen) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM \"%s\";", s->view_name);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, errlen, sqlite3_errmsg(s->db)); return -1;
    }
    long long n = -1;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int seekdb_save_as(seekdb *s, const char *dest_path, char *err, size_t errlen) {
    char esc[1024]; size_t j=0;
    for (size_t i=0; dest_path[i] && j<sizeof(esc)-2; ++i) {
        if (dest_path[i]=='\'') { esc[j++]='\''; esc[j++]='\''; }
        else esc[j++]=dest_path[i];
    }
    esc[j]='\0';

    char sql[1200];
    snprintf(sql, sizeof(sql), "VACUUM INTO '%s';", esc);
    return exec_sql(s->db, sql, err, errlen) == SQLITE_OK ? 0 : -1;
}

void seekdb_close(seekdb *s) {
    if (!s) return;
    if (s->db) sqlite3_close(s->db);
    free(s->view_name);
    free(s->key_name);
    if (s->tmpdir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/spill.db", s->tmpdir);
        (void)unlink(path);
        (void)rmdir(s->tmpdir);
        free(s->tmpdir);
    }
    free(s);
}

int seekdb_get_view_columns(seekdb *s, char ***names_out, char *err, size_t errlen) {
    if (!s || !s->db || !names_out) { set_err(err, errlen, "bad args"); return -1; }
    *names_out = NULL;
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT * FROM \"%s\" LIMIT 0;", s->view_name);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_err(err, errlen, sqlite3_errmsg(s->db)); return -1;
    }
    int n = sqlite3_column_count(st);
    char **arr = (char**)calloc((size_t)n, sizeof(char*));
    for (int i = 0; i < n; ++i) {
        const char *nm = sqlite3_column_name(st, i);
        arr[i] = strdup(nm ? nm : "");
    }
    sqlite3_finalize(st);
    *names_out = arr;
    return n;
}
