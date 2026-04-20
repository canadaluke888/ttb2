/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Search-mode navigation and async query handling. */

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "ui/internal.h"
#include "core/errors.h"
#include "core/task_worker.h"
#include "core/workspace.h"

typedef struct {
    int row;
    int col;
    int start;
    int len;
    float score;
} SearchHit;

static SearchHit *hits = NULL;
static int search_pending = 0;

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

static int *build_visible_row_map(Table *table)
{
    int visible_rows;
    int *map;
    int i;

    if (!table || table->row_count < 0) return NULL;

    map = (int *)malloc(sizeof(int) * (size_t)(table->row_count > 0 ? table->row_count : 1));
    if (!map) return NULL;

    for (i = 0; i < table->row_count; ++i) {
        map[i] = -1;
    }

    visible_rows = ui_visible_row_count(table);
    for (i = 0; i < visible_rows; ++i) {
        int actual_row = ui_actual_row_for_visible(table, i);

        if (actual_row >= 0 && actual_row < table->row_count) {
            map[actual_row] = i;
        }
    }

    return map;
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

static int queue_search_job(Table *table, const char *query, char *err, size_t err_sz)
{
    Table *snapshot = NULL;
    int visible_rows;
    int *actual_rows = NULL;
    int i;

    if (!table || !query || !*query) {
        if (err && err_sz > 0) {
            strncpy(err, "Invalid search request", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    visible_rows = ui_visible_row_count(table);
    if (visible_rows <= 0) return 0;

    snapshot = clone_table(table);
    if (!snapshot) {
        if (err && err_sz > 0) {
            strncpy(err, "Out of memory", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    actual_rows = (int *)calloc((size_t)visible_rows, sizeof(int));
    if (!actual_rows) {
        free_table(snapshot);
        if (err && err_sz > 0) {
            strncpy(err, "Out of memory", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    for (i = 0; i < visible_rows; ++i) {
        actual_rows[i] = ui_actual_row_for_visible(table, i);
    }

    if (task_worker_start_search(snapshot,
                                 actual_rows,
                                 visible_rows,
                                 workspace_project_path(),
                                 workspace_active_table_id(),
                                 query,
                                 err,
                                 err_sz) != 0) {
        free(actual_rows);
        free_table(snapshot);
        return -1;
    }

    return 1;
}

void ui_search_enter(Table *table)
{
    char query[128] = {0};
    char err[256] = {0};

    if (search_pending) return;

    if (show_text_input_modal("Substring Search",
                              "[Enter] Search visible rows   [Esc] Cancel",
                              "Query: ",
                              query,
                              sizeof(query),
                              false) <= 0) {
        return;
    }

    trim_ascii(query);
    if (query[0] == '\0') return;

    ui_search_exit();
    {
        int queue_status = queue_search_job(table, query, err, sizeof(err));

        if (queue_status < 0) {
            show_error_message(err[0] ? err : "Search failed.");
            return;
        }
        if (queue_status == 0) {
            show_error_message("No substring matches found.");
            return;
        }
    }

    strncpy(search_query, query, sizeof(search_query) - 1);
    search_query[sizeof(search_query) - 1] = '\0';
    del_row_mode = 0;
    del_col_mode = 0;
    ui_clear_reorder_mode();
    footer_page = 0;
    search_pending = 1;
    ui_footer_activity_start();
}

void ui_search_exit(void)
{
    search_mode = 0;
    clear_search_hits();
}

void ui_search_poll(Table *table)
{
    TaskWorkerSearchResult result = {0};
    char err[256] = {0};
    int *visible_row_map = NULL;
    int status;
    int i;

    if (!search_pending) return;

    status = task_worker_take_search_result(&result, err, sizeof(err));
    if (status == 0) return;

    search_pending = 0;

    if (status < 0) {
        ui_footer_activity_stop();
        show_error_message(err[0] ? err : "Search failed.");
        return;
    }

    clear_search_hits();
    if (result.count > 0) {
        visible_row_map = build_visible_row_map(table);
        hits = (SearchHit *)calloc((size_t)result.count, sizeof(*hits));
        if (!hits || !visible_row_map) {
            free(visible_row_map);
            task_worker_free_search_result(&result);
            ui_footer_activity_stop();
            show_error_message("Out of memory");
            return;
        }

        for (i = 0; i < result.count; ++i) {
            int actual_row = result.matches[i].actual_row;
            int visible_row = (actual_row >= 0 && actual_row < table->row_count)
                ? visible_row_map[actual_row]
                : -1;

            if (visible_row < 0) continue;
            hits[search_hit_count].row = visible_row;
            hits[search_hit_count].col = result.matches[i].best_col;
            hits[search_hit_count].start = result.matches[i].match_start;
            hits[search_hit_count].len = result.matches[i].match_len;
            hits[search_hit_count].score = result.matches[i].score;
            search_hit_count++;
        }
        free(visible_row_map);
    }

    task_worker_free_search_result(&result);
    ui_footer_activity_stop();

    if (search_hit_count <= 0) {
        show_error_message("No substring matches found.");
        return;
    }

    search_mode = 1;
    apply_search_hit(0);
}

int ui_search_pending(void)
{
    return search_pending || task_worker_search_pending();
}

int ui_search_handle_key(Table *table, int ch)
{
    (void)table;

    if (ui_search_pending()) {
        if (ch == 27) return 1;
        return 1;
    }

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
