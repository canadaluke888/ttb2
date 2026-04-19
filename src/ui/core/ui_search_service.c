/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Search service that bridges UI queries to the semantic table index. */

#include <stdlib.h>
#include <string.h>

#include "ui/internal.h"
#include "core/workspace.h"
#include "db/book_db.h"
#include "vector/table_index.h"

static TableIndex *g_index = NULL;
static char g_table_id[256] = "";

void ui_search_service_reset(void)
{
    table_index_invalidate(&g_index);
    g_table_id[0] = '\0';
}

int ui_search_service_query(Table *table, const char *query, UiSearchResult **out_results, int *out_count, char *err, size_t err_sz)
{
    return ui_search_service_query_with_progress(table, query, NULL, out_results, out_count, err, err_sz);
}

int ui_search_service_query_with_progress(Table *table,
                                          const char *query,
                                          const ProgressReporter *progress,
                                          UiSearchResult **out_results,
                                          int *out_count,
                                          char *err,
                                          size_t err_sz)
{
    BookDB *db = NULL;
    TableIndexConfig config;
    const char *table_id;
    int visible_rows;
    int *actual_rows = NULL;
    TableIndexMatch *matches = NULL;
    UiSearchResult *results = NULL;
    int i;
    int match_count = 0;

    if (out_results) *out_results = NULL;
    if (out_count) *out_count = 0;
    if (!table || !query || !*query || !out_results || !out_count) {
        if (err && err_sz > 0) {
            strncpy(err, "Invalid search request", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    table_id = workspace_active_table_id();
    if (table_id && *table_id && strcmp(g_table_id, table_id) != 0) {
        ui_search_service_reset();
    }

    config = table_index_default_config();
    if (table_id && *table_id) {
        char load_err[256] = {0};

        db = bookdb_open(workspace_project_path(), 0, load_err, sizeof(load_err));
        if (db && bookdb_init_schema(db, load_err, sizeof(load_err)) != 0) {
            bookdb_close(db);
            db = NULL;
        }
    }

    if (table_index_sync_bookdb_with_progress(db, table_id, table, &config, progress, &g_index, err, err_sz) != 0) {
        if (db) bookdb_close(db);
        return -1;
    }
    if (db) bookdb_close(db);

    if (table_id && *table_id) {
        strncpy(g_table_id, table_id, sizeof(g_table_id) - 1);
        g_table_id[sizeof(g_table_id) - 1] = '\0';
    } else {
        g_table_id[0] = '\0';
    }

    visible_rows = ui_visible_row_count(table);
    if (visible_rows <= 0) return 0;

    actual_rows = (int *)calloc((size_t)visible_rows, sizeof(int));
    matches = (TableIndexMatch *)calloc((size_t)visible_rows, sizeof(*matches));
    if (!actual_rows || !matches) {
        free(actual_rows);
        free(matches);
        if (err && err_sz > 0) {
            strncpy(err, "Out of memory", err_sz - 1);
            err[err_sz - 1] = '\0';
        }
        return -1;
    }

    for (i = 0; i < visible_rows; ++i) {
        actual_rows[i] = ui_actual_row_for_visible(table, i);
    }

    match_count = table_index_query_with_progress(g_index,
                                                  table,
                                                  actual_rows,
                                                  visible_rows,
                                                  query,
                                                  matches,
                                                  (size_t)visible_rows,
                                                  progress,
                                                  err,
                                                  err_sz);
    if (match_count < 0) {
        free(actual_rows);
        free(matches);
        return -1;
    }

    if (match_count > 0) {
        results = (UiSearchResult *)calloc((size_t)match_count, sizeof(*results));
        if (!results) {
            free(actual_rows);
            free(matches);
            if (err && err_sz > 0) {
                strncpy(err, "Out of memory", err_sz - 1);
                err[err_sz - 1] = '\0';
            }
            return -1;
        }
        for (i = 0; i < match_count; ++i) {
            results[i].actual_row = matches[i].actual_row;
            results[i].best_col = matches[i].best_col;
            results[i].match_start = matches[i].match_start;
            results[i].match_len = matches[i].match_len;
            results[i].score = matches[i].score;
            results[i].lexical_score = matches[i].lexical_score;
            results[i].semantic_score = matches[i].semantic_score;
        }
    }

    free(actual_rows);
    free(matches);
    *out_results = results;
    *out_count = match_count;
    return 0;
}
