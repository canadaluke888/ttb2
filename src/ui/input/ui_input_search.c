/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Search-mode navigation and query handling. */

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "ui/internal.h"
#include "core/errors.h"

typedef struct {
    int row;
    int col;
    int start;
    int len;
    float score;
} SearchHit;

static SearchHit *hits = NULL;

static void trim_ascii(char *s)
{
    int i;
    int n;

    if (!s) return;
    i = 0;
    while (s[i] && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void clear_search_hits(void)
{
    free(hits);
    hits = NULL;
    search_hit_count = 0;
    search_hit_index = 0;
    search_query[0] = '\0';
    search_sel_start = -1;
    search_sel_len = 0;
}

static int visible_row_for_actual(Table *table, int actual_row)
{
    int visible_rows;
    int i;

    if (!table) return -1;
    visible_rows = ui_visible_row_count(table);
    for (i = 0; i < visible_rows; ++i) {
        if (ui_actual_row_for_visible(table, i) == actual_row) return i;
    }
    return -1;
}

static void apply_search_hit(int index)
{
    if (index < 0 || index >= search_hit_count) return;
    search_hit_index = index;
    cursor_row = hits[index].row;
    cursor_col = hits[index].col;
    search_sel_start = hits[index].start;
    search_sel_len = hits[index].len;
    if (rows_visible > 0) row_page = cursor_row / rows_visible;
}

static int gather_search_hits(Table *table, const char *query, char *err, size_t err_sz)
{
    UiSearchResult *results = NULL;
    int result_count = 0;
    int i;

    clear_search_hits();
    if (!table) return -1;

    strncpy(search_query, query ? query : "", sizeof(search_query) - 1);
    search_query[sizeof(search_query) - 1] = '\0';
    trim_ascii(search_query);
    if (search_query[0] == '\0') return 0;

    if (ui_search_service_query(table, search_query, &results, &result_count, err, err_sz) != 0) {
        free(results);
        return -1;
    }
    if (result_count <= 0) {
        free(results);
        return 0;
    }

    hits = (SearchHit *)calloc((size_t)result_count, sizeof(*hits));
    if (!hits) {
        free(results);
        clear_search_hits();
        if (err && err_sz > 0) {
            strncpy(err, "Out of memory", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    for (i = 0; i < result_count; ++i) {
        int visible_row = visible_row_for_actual(table, results[i].actual_row);

        if (visible_row < 0) continue;
        hits[search_hit_count].row = visible_row;
        hits[search_hit_count].col = results[i].best_col;
        hits[search_hit_count].start = results[i].match_start;
        hits[search_hit_count].len = results[i].match_len;
        hits[search_hit_count].score = results[i].score;
        search_hit_count++;
    }

    free(results);
    return search_hit_count;
}

void ui_search_enter(Table *table)
{
    char query[128] = {0};
    char err[256] = {0};

    if (show_text_input_modal("Hybrid Search",
                              "[Enter] Search visible rows   [Esc] Cancel",
                              "Query: ",
                              query,
                              sizeof(query),
                              false) <= 0) {
        return;
    }
    if (gather_search_hits(table, query, err, sizeof(err)) < 0) {
        show_error_message(err[0] ? err : "Search failed.");
        return;
    }
    if (search_hit_count <= 0) {
        show_error_message("No ranked matches found.");
        return;
    }

    del_row_mode = 0;
    del_col_mode = 0;
    ui_clear_reorder_mode();
    footer_page = 0;
    search_mode = 1;
    apply_search_hit(0);
}

void ui_search_exit(void)
{
    search_mode = 0;
    clear_search_hits();
}

int ui_search_handle_key(Table *table, int ch)
{
    if (!search_mode) return 0;

    if (ch == KEY_LEFT || ch == KEY_UP) {
        if (search_hit_count > 0) {
            int next = (search_hit_index > 0) ? (search_hit_index - 1) : (search_hit_count - 1);
            apply_search_hit(next);
        }
        return 1;
    }

    if (ch == KEY_RIGHT || ch == KEY_DOWN) {
        if (search_hit_count > 0) {
            int next = (search_hit_index + 1) % search_hit_count;
            apply_search_hit(next);
        }
        return 1;
    }

    if (ch == 27) {
        ui_search_exit();
        return 1;
    }

    if (ch == 'f' || ch == 'F') {
        ui_search_enter(table);
        return 1;
    }

    return 1;
}
