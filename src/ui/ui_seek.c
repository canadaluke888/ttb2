#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "seekdb.h"
#include "errors.h"

typedef struct {
    seekdb *s;
    char table[256];
    char key[64];
    int key_col; // index in current view, -1 unknown
    long long first_id;
    long long last_id;
    int active;
    long long row_base;   // 1-based index of first row in current window
    int last_count;       // size of last window fetched
} SeekSession;

static SeekSession G = {0};

// low_ram_mode declared and defined in ui_loop.c

static void clear_table_rows(Table *t) {
    if (!t) return;
    for (int i = 0; i < t->row_count; ++i) {
        if (t->rows[i].values) {
            for (int j = 0; j < t->column_count; ++j) {
                if (t->rows[i].values[j]) free(t->rows[i].values[j]);
            }
            free(t->rows[i].values);
        }
    }
    free(t->rows);
    t->rows = NULL; t->row_count = 0; t->capacity_rows = 0;
}

static void clear_table_columns(Table *t) {
    if (!t) return;
    for (int j = 0; j < t->column_count; ++j) if (t->columns[j].name) free(t->columns[j].name);
    free(t->columns); t->columns = NULL; t->column_count = 0; t->capacity_columns = 0;
}

static void ensure_key_col(const char *const*names, int n) {
    G.key_col = -1;
    for (int i = 0; i < n; ++i) {
        if (strcmp(names[i], G.key) == 0) { G.key_col = i; break; }
    }
}

static int fill_columns(Table *view, char **names, int count) {
    // All columns are displayed as strings for the view; types can be refined later
    clear_table_columns(view);
    for (int i = 0; i < count; ++i) {
        add_column(view, names[i], TYPE_STR);
    }
    return 0;
}

typedef struct { Table *t; int reverse; long long first; long long last; } FillCtx;

static bool stream_row(void *user, sqlite3_stmt *row) {
    FillCtx *fc = (FillCtx*)user;
    int cols = sqlite3_column_count(row);
    char **vals = (char**)malloc(sizeof(char*) * cols);
    for (int j = 0; j < cols; ++j) {
        const unsigned char *txt = sqlite3_column_text(row, j);
        if (txt) vals[j] = strdup((const char*)txt);
        else vals[j] = strdup("");
    }
    add_row(fc->t, (const char**)vals);
    // track ids
    int key_idx = G.key_col;
    if (key_idx >= 0) {
        sqlite3_int64 id = sqlite3_column_int64(row, key_idx);
        if (fc->t->row_count == 1) fc->first = id; // first row delivered
        fc->last = id;
    }
    for (int j = 0; j < cols; ++j) { free(vals[j]); }
    free(vals);
    return true;
}

// For seek_before, accumulate then reverse to ascending order for display
typedef struct { int cols; int cap; int n; char ***rows; long long *ids; } Buf;

static void buf_init(Buf *b, int cols) { b->cols = cols; b->cap = 0; b->n = 0; b->rows = NULL; b->ids = NULL; }
static void buf_push(Buf *b, sqlite3_stmt *row, int key_idx) {
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 16;
        b->rows = (char***)realloc(b->rows, sizeof(char**) * b->cap);
        b->ids = (long long*)realloc(b->ids, sizeof(long long) * b->cap);
    }
    char **vals = (char**)malloc(sizeof(char*) * b->cols);
    for (int j = 0; j < b->cols; ++j) {
        const unsigned char *txt = sqlite3_column_text(row, j);
        vals[j] = strdup(txt ? (const char*)txt : "");
    }
    b->rows[b->n] = vals;
    b->ids[b->n] = (key_idx >= 0) ? sqlite3_column_int64(row, key_idx) : 0;
    b->n++;
}
static void buf_free(Buf *b) {
    for (int i = 0; i < b->n; ++i) { for (int j = 0; j < b->cols; ++j) free(b->rows[i][j]); free(b->rows[i]); }
    free(b->rows); free(b->ids);
}

static bool collect_row(void *user, sqlite3_stmt *row) {
    Buf *b = (Buf*)user; buf_push(b, row, G.key_col); return true;
}

int seek_mode_active(void) { return G.active; }

void seek_mode_close(void) {
    if (G.s) { seekdb_close(G.s); G.s = NULL; }
    G.active = 0; G.key_col = -1; G.first_id = 0; G.last_id = 0;
}

int seek_mode_open_for_table(const char *db_path, const char *table_name, Table *view, int page_size, char *err, size_t err_sz) {
    seek_mode_close();
    G.s = seekdb_open(db_path, SEEKDB_MODE_LOW_RAM, err, err_sz);
    if (!G.s) return -1;
    snprintf(G.table, sizeof(G.table), "%s", table_name);
    snprintf(G.key, sizeof(G.key), "%s", "_ttb_id");
    if (seekdb_ensure_stable_key(G.s, G.table, G.key, err, err_sz) != 0) return -1;
    if (seekdb_set_view(G.s, G.table, "1=1", G.key, G.key, err, err_sz) != 0) return -1;
    char **names = NULL; int n = seekdb_get_view_columns(G.s, &names, err, err_sz);
    if (n <= 0) return -1;
    ensure_key_col((const char* const*)names, n);
    // Reset and fill columns
    if (view->name) { free(view->name); view->name = NULL; }
    view->name = strdup(table_name);
    fill_columns(view, names, n);
    for (int i = 0; i < n; ++i) { free(names[i]); }
    free(names);
    // Fetch first page
    return seek_mode_fetch_first(view, page_size, err, err_sz);
}

int seek_mode_fetch_first(Table *view, int page_size, char *err, size_t err_sz) {
    clear_table_rows(view);
    FillCtx ctx = { .t = view, .reverse = 0, .first = 0, .last = 0 };
    int got = seekdb_seek_first(G.s, page_size, stream_row, &ctx, err, err_sz);
    if (got < 0) return -1;
    G.first_id = ctx.first; G.last_id = ctx.last; G.active = 1;
    G.row_base = 1; G.last_count = got;
    return got;
}

int seek_mode_fetch_next(Table *view, int page_size, char *err, size_t err_sz) {
    clear_table_rows(view);
    FillCtx ctx = { .t = view, .reverse = 0, .first = 0, .last = 0 };
    int got = seekdb_seek_after(G.s, G.last_id, page_size, stream_row, &ctx, err, err_sz);
    if (got < 0) return -1;
    if (got == 0) return 0;
    G.row_base += (G.last_count > 0 ? G.last_count : 0);
    G.last_count = got;
    if (G.row_base < 1) G.row_base = 1;
    G.first_id = ctx.first; G.last_id = ctx.last;
    return got;
}

int seek_mode_fetch_prev(Table *view, int page_size, char *err, size_t err_sz) {
    // Collect in buffer (descending), then reverse into table
    clear_table_rows(view);
    char err2[256] = {0};
    // Peek column count via current view
    char **names = NULL; int cols = seekdb_get_view_columns(G.s, &names, err2, sizeof err2);
    for (int i = 0; i < cols; ++i) { free(names[i]); }
    free(names);
    Buf b; buf_init(&b, cols > 0 ? cols : view->column_count);
    int got = seekdb_seek_before(G.s, G.first_id, page_size, collect_row, &b, err, err_sz);
    if (got < 0) { buf_free(&b); return -1; }
    if (b.n == 0) { buf_free(&b); return 0; }
    // Reverse append
    G.first_id = b.ids[b.n-1];
    G.last_id = b.ids[0];
    for (int i = b.n - 1; i >= 0; --i) {
        add_row(view, (const char**)b.rows[i]);
    }
    // Update row base
    G.row_base -= b.n;
    if (G.row_base < 1) G.row_base = 1;
    G.last_count = b.n;
    buf_free(&b);
    return got;
}

long long seek_mode_row_base(void) { return G.active ? (G.row_base > 0 ? G.row_base : 1) : 1; }
int seek_mode_last_count(void) { return G.active ? (G.last_count > 0 ? G.last_count : 0) : 0; }
