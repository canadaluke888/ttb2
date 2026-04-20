/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Single background worker thread for blocking search-style tasks. */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "core/task_worker.h"
#include "core/settings.h"
#include "db/book_db.h"
#include "vector/table_index.h"

typedef enum {
    SEARCH_JOB_NONE = 0,
    SEARCH_JOB_PENDING,
    SEARCH_JOB_RUNNING,
    SEARCH_JOB_READY,
    SEARCH_JOB_ERROR
} SearchJobState;

typedef struct {
    Table *table_snapshot;
    int *actual_rows;
    int actual_row_count;
    char *project_path;
    char *table_id;
    char query[128];
} SearchJob;

typedef struct {
    TaskWorkerSearchMatch *matches;
    int count;
    char err[256];
} SearchJobResult;

static pthread_t g_worker_thread;
static pthread_mutex_t g_worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_worker_cond = PTHREAD_COND_INITIALIZER;
static int g_worker_initialized = 0;
static int g_worker_shutdown_requested = 0;
static SearchJobState g_search_state = SEARCH_JOB_NONE;
static SearchJob g_search_job = {0};
static SearchJobResult g_search_result = {0};

static void search_job_clear(SearchJob *job)
{
    if (!job) return;
    free_table(job->table_snapshot);
    free(job->actual_rows);
    free(job->project_path);
    free(job->table_id);
    memset(job, 0, sizeof(*job));
}

static void search_result_clear(SearchJobResult *result)
{
    if (!result) return;
    free(result->matches);
    memset(result, 0, sizeof(*result));
}

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static void *task_worker_main(void *unused)
{
    TableIndex *cached_index = NULL;
    char cached_table_id[256] = "";

    (void)unused;

    while (1) {
        SearchJob job = {0};
        SearchJobResult result = {0};

        pthread_mutex_lock(&g_worker_mutex);
        while (!g_worker_shutdown_requested && g_search_state != SEARCH_JOB_PENDING) {
            pthread_cond_wait(&g_worker_cond, &g_worker_mutex);
        }
        if (g_worker_shutdown_requested) {
            pthread_mutex_unlock(&g_worker_mutex);
            break;
        }

        job = g_search_job;
        memset(&g_search_job, 0, sizeof(g_search_job));
        g_search_state = SEARCH_JOB_RUNNING;
        pthread_mutex_unlock(&g_worker_mutex);

        if (!job.table_snapshot || !job.actual_rows || job.actual_row_count < 0 || job.query[0] == '\0') {
            strncpy(result.err, "Invalid search job", sizeof(result.err) - 1);
        } else {
            BookDB *db = NULL;
            TableIndexConfig config = table_index_default_config();
            TableIndexMatch *matches = NULL;
            int match_count = -1;

            config.row_vectorization_enabled = settings_row_vectorization_enabled() ? 1 : 0;

            if (job.table_id && *job.table_id && strcmp(cached_table_id, job.table_id) != 0) {
                table_index_invalidate(&cached_index);
                cached_table_id[0] = '\0';
            }

            if (job.project_path && *job.project_path) {
                char db_err[256] = {0};

                db = bookdb_open(job.project_path, 0, db_err, sizeof(db_err));
                if (db && bookdb_init_schema(db, db_err, sizeof(db_err)) != 0) {
                    bookdb_close(db);
                    db = NULL;
                }
            }

            if (table_index_sync_bookdb(db,
                                        job.table_id,
                                        job.table_snapshot,
                                        &config,
                                        &cached_index,
                                        result.err,
                                        sizeof(result.err)) == 0) {
                matches = (TableIndexMatch *)calloc((size_t)(job.actual_row_count > 0 ? job.actual_row_count : 1),
                                                    sizeof(*matches));
                if (!matches) {
                    strncpy(result.err, "Out of memory", sizeof(result.err) - 1);
                } else {
                    match_count = table_index_query(cached_index,
                                                    job.table_snapshot,
                                                    job.actual_rows,
                                                    job.actual_row_count,
                                                    job.query,
                                                    matches,
                                                    (size_t)job.actual_row_count,
                                                    result.err,
                                                    sizeof(result.err));
                    if (match_count >= 0) {
                        int i;

                        result.count = match_count;
                        if (match_count > 0) {
                            result.matches = (TaskWorkerSearchMatch *)calloc((size_t)match_count, sizeof(*result.matches));
                            if (!result.matches) {
                                strncpy(result.err, "Out of memory", sizeof(result.err) - 1);
                                result.count = 0;
                            } else {
                                for (i = 0; i < match_count; ++i) {
                                    result.matches[i].actual_row = matches[i].actual_row;
                                    result.matches[i].best_col = matches[i].best_col;
                                    result.matches[i].match_start = matches[i].match_start;
                                    result.matches[i].match_len = matches[i].match_len;
                                    result.matches[i].score = matches[i].score;
                                    result.matches[i].lexical_score = matches[i].lexical_score;
                                    result.matches[i].semantic_score = matches[i].semantic_score;
                                }
                            }
                        }
                    }
                }
                free(matches);
            }

            if (db) bookdb_close(db);
            if (job.table_id && *job.table_id && result.err[0] == '\0') {
                strncpy(cached_table_id, job.table_id, sizeof(cached_table_id) - 1);
                cached_table_id[sizeof(cached_table_id) - 1] = '\0';
            }
        }

        search_job_clear(&job);

        pthread_mutex_lock(&g_worker_mutex);
        search_result_clear(&g_search_result);
        g_search_result = result;
        g_search_state = result.err[0] ? SEARCH_JOB_ERROR : SEARCH_JOB_READY;
        pthread_mutex_unlock(&g_worker_mutex);
    }

    table_index_invalidate(&cached_index);
    return NULL;
}

int task_worker_init(char *err, size_t err_sz)
{
    if (g_worker_initialized) return 0;

    g_worker_shutdown_requested = 0;
    g_search_state = SEARCH_JOB_NONE;
    memset(&g_search_job, 0, sizeof(g_search_job));
    memset(&g_search_result, 0, sizeof(g_search_result));

    if (pthread_create(&g_worker_thread, NULL, task_worker_main, NULL) != 0) {
        set_err(err, err_sz, "Failed to start worker thread");
        return -1;
    }

    g_worker_initialized = 1;
    return 0;
}

void task_worker_shutdown(void)
{
    if (!g_worker_initialized) return;

    pthread_mutex_lock(&g_worker_mutex);
    g_worker_shutdown_requested = 1;
    pthread_cond_signal(&g_worker_cond);
    pthread_mutex_unlock(&g_worker_mutex);

    pthread_join(g_worker_thread, NULL);

    pthread_mutex_lock(&g_worker_mutex);
    search_job_clear(&g_search_job);
    search_result_clear(&g_search_result);
    g_search_state = SEARCH_JOB_NONE;
    pthread_mutex_unlock(&g_worker_mutex);

    g_worker_initialized = 0;
}

int task_worker_start_search(Table *table_snapshot,
                             int *actual_rows,
                             int actual_row_count,
                             const char *project_path,
                             const char *table_id,
                             const char *query,
                             char *err,
                             size_t err_sz)
{
    SearchJob next_job = {0};

    if (!g_worker_initialized) {
        set_err(err, err_sz, "Worker thread is not initialized");
        return -1;
    }
    if (!table_snapshot || !actual_rows || actual_row_count < 0 || !query || query[0] == '\0') {
        search_job_clear(&next_job);
        set_err(err, err_sz, "Invalid search request");
        return -1;
    }

    next_job.table_snapshot = table_snapshot;
    next_job.actual_rows = actual_rows;
    next_job.actual_row_count = actual_row_count;
    next_job.project_path = project_path ? strdup(project_path) : NULL;
    next_job.table_id = table_id ? strdup(table_id) : NULL;
    strncpy(next_job.query, query, sizeof(next_job.query) - 1);
    next_job.query[sizeof(next_job.query) - 1] = '\0';

    if ((project_path && !next_job.project_path) || (table_id && !next_job.table_id)) {
        search_job_clear(&next_job);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    pthread_mutex_lock(&g_worker_mutex);
    if (g_search_state == SEARCH_JOB_PENDING || g_search_state == SEARCH_JOB_RUNNING) {
        pthread_mutex_unlock(&g_worker_mutex);
        search_job_clear(&next_job);
        set_err(err, err_sz, "A background task is already running");
        return -1;
    }

    search_job_clear(&g_search_job);
    search_result_clear(&g_search_result);
    g_search_job = next_job;
    g_search_state = SEARCH_JOB_PENDING;
    pthread_cond_signal(&g_worker_cond);
    pthread_mutex_unlock(&g_worker_mutex);
    return 0;
}

int task_worker_search_pending(void)
{
    int pending;

    pthread_mutex_lock(&g_worker_mutex);
    pending = (g_search_state == SEARCH_JOB_PENDING || g_search_state == SEARCH_JOB_RUNNING);
    pthread_mutex_unlock(&g_worker_mutex);
    return pending;
}

int task_worker_take_search_result(TaskWorkerSearchResult *out_result,
                                   char *err,
                                   size_t err_sz)
{
    int ready = 0;

    if (out_result) memset(out_result, 0, sizeof(*out_result));

    pthread_mutex_lock(&g_worker_mutex);
    if (g_search_state == SEARCH_JOB_READY) {
        if (out_result) {
            out_result->matches = g_search_result.matches;
            out_result->count = g_search_result.count;
        } else {
            free(g_search_result.matches);
        }
        g_search_result.matches = NULL;
        g_search_result.count = 0;
        g_search_result.err[0] = '\0';
        g_search_state = SEARCH_JOB_NONE;
        ready = 1;
    } else if (g_search_state == SEARCH_JOB_ERROR) {
        set_err(err, err_sz, g_search_result.err[0] ? g_search_result.err : "Background task failed");
        search_result_clear(&g_search_result);
        g_search_state = SEARCH_JOB_NONE;
        ready = -1;
    }
    pthread_mutex_unlock(&g_worker_mutex);

    return ready;
}

void task_worker_free_search_result(TaskWorkerSearchResult *result)
{
    if (!result) return;
    free(result->matches);
    result->matches = NULL;
    result->count = 0;
}
