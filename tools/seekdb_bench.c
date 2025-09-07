#include "seekdb.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long long now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long get_rss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long kb = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            char *p = line; while (*p && (*p < '0' || *p > '9')) p++;
            kb = strtol(p, NULL, 10); break;
        }
    }
    fclose(f); return kb;
}

typedef struct {
    int cols;
    long delivered;
} cb_ctx;

static bool count_rows(void *user, sqlite3_stmt *row) {
    (void)row;
    cb_ctx *ctx = (cb_ctx*)user;
    ctx->delivered++;
    return true; // deliver full window
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [rows=200000] [page=200]\n", argv0);
}

int main(int argc, char **argv) {
    long rows = 200000; // default 200k for quick run
    int page = 200;     // default window size
    if (argc > 1) rows = strtol(argv[1], NULL, 10);
    if (argc > 2) page = (int)strtol(argv[2], NULL, 10);
    if (rows <= 0 || page <= 0) { usage(argv[0]); return 2; }

    char err[256] = {0};
    seekdb *s = seekdb_open(NULL, SEEKDB_MODE_LOW_RAM, err, sizeof err);
    if (!s) { fprintf(stderr, "open failed: %s\n", err); return 1; }

    // Create simple table
    if (seekdb_ensure_table(s, "t", "val INTEGER, name TEXT", err, sizeof err) != 0) {
        fprintf(stderr, "ensure_table failed: %s\n", err); return 1;
    }

    // Bulk insert rows in a single transaction for speed
    sqlite3 *db = NULL; // access internal db via header? Not exposed; prepare using sqlite3 API not available here.
    // Work around by opening a new handle to the temp DB file is not exposed either; instead use a simple approach:
    // Prepare insert with sqlite3_exec via multiple statements; slower but fine for smoke. For speed, keep <= 200k default.
    {
        // Create a temporary table to speed name generation
        // Start transaction
        if (seekdb_set_view(s, "t", "1=0", "_ttb_id", "_ttb_id", err, sizeof err) < 0) {
            // ignore view error before key exists; proceed with inserts then key
        }
        // Use sqlite3_exec
        // Begin
        sqlite3 *conn = NULL;
        // Ugly: reach internal by casting; documented for bench only.
        conn = *(sqlite3**)((char*)s + 0); // relies on struct layout: first field sqlite3* db
        if (!conn) { fprintf(stderr, "internal db missing\n"); return 1; }
        char *emsg = NULL;
        if (sqlite3_exec(conn, "BEGIN;", NULL, NULL, &emsg) != SQLITE_OK) {
            fprintf(stderr, "BEGIN failed: %s\n", emsg?emsg:"err"); if (emsg) sqlite3_free(emsg); return 1;
        }
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(conn, "INSERT INTO t(val, name) VALUES(?, ?);", -1, &st, NULL) != SQLITE_OK) {
            fprintf(stderr, "prepare failed: %s\n", sqlite3_errmsg(conn)); return 1;
        }
        for (long i = 1; i <= rows; ++i) {
            sqlite3_reset(st);
            sqlite3_bind_int64(st, 1, (sqlite3_int64)i);
            char buf[32]; snprintf(buf, sizeof(buf), "row%ld", i);
            sqlite3_bind_text(st, 2, buf, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) != SQLITE_DONE) {
                fprintf(stderr, "insert failed at %ld: %s\n", i, sqlite3_errmsg(conn)); return 1;
            }
        }
        sqlite3_finalize(st);
        if (sqlite3_exec(conn, "COMMIT;", NULL, NULL, &emsg) != SQLITE_OK) {
            fprintf(stderr, "COMMIT failed: %s\n", emsg?emsg:"err"); if (emsg) sqlite3_free(emsg); return 1;
        }
        if (emsg) sqlite3_free(emsg);
    }

    if (seekdb_ensure_stable_key(s, "t", "_ttb_id", err, sizeof err) != 0) {
        fprintf(stderr, "ensure_key failed: %s\n", err); return 1;
    }
    if (seekdb_set_view(s, "t", "1=1", "val ASC", "_ttb_id", err, sizeof err) != 0) {
        fprintf(stderr, "set_view failed: %s\n", err); return 1;
    }

    cb_ctx ctx = {0};
    long rss0 = get_rss_kb();

    long long t0 = now_ns();
    // Simulate scroll down: first page then repeated after
    ctx.delivered = 0;
    long long last_id = 0;
    int n = 0;
    int got = 0;
    // first page
    got = seekdb_seek_first(s, page, count_rows, &ctx, err, sizeof err);
    if (got < 0) { fprintf(stderr, "seek_first error: %s\n", err); return 1; }
    n += got; last_id = page; // because val asc matches _ttb_id after backfill

    while (n < rows) {
        got = seekdb_seek_after(s, last_id, page, count_rows, &ctx, err, sizeof err);
        if (got < 0) { fprintf(stderr, "seek_after error: %s\n", err); return 1; }
        if (got == 0) break;
        n += got;
        last_id += got;
        if (n >= rows) break;
    }
    long long t1 = now_ns();
    long rss1 = get_rss_kb();

    double sec = (t1 - t0) / 1e9;
    printf("Rows: %ld, Page: %d, Time: %.3fs, Throughput: %.1f rows/s\n", rows, page, sec, rows / sec);
    printf("RSS start: %ld KB, RSS end: %ld KB, Delta: %ld KB\n", rss0, rss1, (rss1>=0&&rss0>=0)?(rss1-rss0):-1);

    // Quick upward paging smoke from near end
    long long first_id = last_id;
    ctx.delivered = 0;
    got = seekdb_seek_before(s, first_id, page, count_rows, &ctx, err, sizeof err);
    if (got < 0) { fprintf(stderr, "seek_before error: %s\n", err); return 1; }
    printf("seek_before delivered: %d\n", got);

    seekdb_close(s);
    return 0;
}

