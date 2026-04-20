/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Deterministic row-vector indexing and hybrid retrieval for table data. */

#ifndef TTB2_TABLE_INDEX_H
#define TTB2_TABLE_INDEX_H

#include <stddef.h>
#include "data/table.h"
#include "core/progress.h"

typedef struct BookDB BookDB;

typedef struct {
    int dimensions;
    int max_tokens_per_row;
    int use_char_trigrams;
    int include_numeric_stats;
    int row_vectorization_enabled;
    float unigram_weight;
    float trigram_weight;
    float lexical_weight;
    float semantic_weight;
    unsigned int config_version;
} TableIndexConfig;

typedef struct {
    int column_index;
    DataType type;
    long long value_count;
    long long null_count;
    double min_value;
    double max_value;
    double mean_value;
    int has_values;
} TableIndexColumnStats;

typedef struct {
    int actual_row;
    int best_col;
    int match_start;
    int match_len;
    float score;
    float lexical_score;
    float semantic_score;
} TableIndexMatch;

typedef struct TableIndex {
    TableIndexConfig config;
    unsigned long long fingerprint;
    int stale;
    int row_count;
    int column_count;
    int dimensions;
    int *row_ids;
    float *embeddings;
    float *norms;
    TableIndexColumnStats *column_stats;
} TableIndex;

TableIndexConfig table_index_default_config(void);

int table_index_build_for_table_with_progress(const Table *table,
                                              const TableIndexConfig *config,
                                              const ProgressReporter *progress,
                                              TableIndex **out_index,
                                              char *err,
                                              size_t err_sz);
int table_index_build_for_table(const Table *table,
                                const TableIndexConfig *config,
                                TableIndex **out_index,
                                char *err,
                                size_t err_sz);
int table_index_query(const TableIndex *index,
                      const Table *table,
                      const int *actual_rows,
                      int actual_row_count,
                      const char *query,
                      TableIndexMatch *out,
                      size_t max_out,
                      char *err,
                      size_t err_sz);
int table_index_query_with_progress(const TableIndex *index,
                                    const Table *table,
                                    const int *actual_rows,
                                    int actual_row_count,
                                    const char *query,
                                    TableIndexMatch *out,
                                    size_t max_out,
                                    const ProgressReporter *progress,
                                    char *err,
                                    size_t err_sz);
void table_index_invalidate(TableIndex **index_ptr);
void table_index_free(TableIndex *index);
int table_index_sync_bookdb_with_progress(BookDB *db,
                                          const char *table_id,
                                          const Table *table,
                                          const TableIndexConfig *config,
                                          const ProgressReporter *progress,
                                          TableIndex **index_in_out,
                                          char *err,
                                          size_t err_sz);
int table_index_sync_bookdb(BookDB *db,
                            const char *table_id,
                            const Table *table,
                            const TableIndexConfig *config,
                            TableIndex **index_in_out,
                            char *err,
                            size_t err_sz);

#endif
