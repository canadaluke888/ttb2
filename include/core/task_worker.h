/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Background worker for offloading blocking tasks from the UI thread. */

#ifndef TASK_WORKER_H
#define TASK_WORKER_H

#include <stddef.h>
#include "data/table.h"

typedef struct {
    int actual_row;
    int best_col;
    int match_start;
    int match_len;
    float score;
    float lexical_score;
    float semantic_score;
} TaskWorkerSearchMatch;

typedef struct {
    TaskWorkerSearchMatch *matches;
    int count;
} TaskWorkerSearchResult;

int task_worker_init(char *err, size_t err_sz);
void task_worker_shutdown(void);

int task_worker_start_search(Table *table_snapshot,
                             int *actual_rows,
                             int actual_row_count,
                             const char *project_path,
                             const char *table_id,
                             const char *query,
                             char *err,
                             size_t err_sz);

int task_worker_search_pending(void);
int task_worker_take_search_result(TaskWorkerSearchResult *out_result,
                                   char *err,
                                   size_t err_sz);
void task_worker_free_search_result(TaskWorkerSearchResult *result);

#endif /* TASK_WORKER_H */
