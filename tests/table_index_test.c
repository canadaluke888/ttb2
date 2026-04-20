/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Regression checks for vector building, ranking, and numeric summaries. */

#include <assert.h>
#include <stdio.h>

#include "data/table.h"
#include "data/table_ops.h"
#include "vector/table_index.h"

static Table *make_sample_table(void)
{
    Table *table = create_table("Products");
    char err[256] = {0};
    const char *row0[] = {"apple", "fruit", "1.50", "10"};
    const char *row1[] = {"orange", "fruit", "2.00", "8"};
    const char *row2[] = {"hammer", "tools", "10.00", "2"};

    assert(table != NULL);
    assert(tableop_insert_column(table, "name", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "category", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "price", TYPE_FLOAT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "stock", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row1, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row2, err, sizeof(err)) == 0);
    return table;
}

static void test_build_and_stats(void)
{
    Table *table = make_sample_table();
    TableIndex *index = NULL;
    char err[256] = {0};

    assert(table_index_build_for_table(table, NULL, &index, err, sizeof(err)) == 0);
    assert(index != NULL);
    assert(index->row_count == 3);
    assert(index->column_count == 4);
    assert(index->dimensions == 256);
    assert(index->fingerprint != 0ULL);
    assert(index->column_stats != NULL);

    assert(index->column_stats[2].has_values == 1);
    assert(index->column_stats[2].value_count == 3);
    assert(index->column_stats[2].null_count == 0);
    assert(index->column_stats[2].min_value < 1.6);
    assert(index->column_stats[2].max_value > 9.9);
    assert(index->column_stats[3].mean_value > 6.5 && index->column_stats[3].mean_value < 6.8);

    table_index_free(index);
    free_table(table);
}

static void test_substring_search_only(void)
{
    Table *table = make_sample_table();
    TableIndex *index = NULL;
    TableIndexMatch matches[3];
    int rows[] = {0, 1, 2};
    char err[256] = {0};
    int count;

    assert(table_index_build_for_table(table, NULL, &index, err, sizeof(err)) == 0);

    count = table_index_query(index, table, rows, 3, "APP", matches, 3, err, sizeof(err));
    assert(count > 0);
    assert(matches[0].actual_row == 0);
    assert(matches[0].best_col == 0);
    assert(matches[0].match_start == 0);
    assert(matches[0].match_len == 3);
    assert(matches[0].lexical_score > 0.0f);
    assert(matches[0].semantic_score == 0.0f);

    count = table_index_query(index, table, rows, 3, "fruit apple", matches, 3, err, sizeof(err));
    assert(count == 0);

    table_index_free(index);
    free_table(table);
}

static void test_build_without_row_vectors(void)
{
    Table *table = make_sample_table();
    TableIndex *index = NULL;
    TableIndexConfig config = table_index_default_config();
    TableIndexMatch matches[3];
    int rows[] = {0, 1, 2};
    char err[256] = {0};
    int count;

    config.row_vectorization_enabled = 0;
    assert(table_index_build_for_table(table, &config, &index, err, sizeof(err)) == 0);
    assert(index != NULL);
    assert(index->row_count == 3);
    assert(index->row_ids == NULL);
    assert(index->embeddings == NULL);
    assert(index->norms == NULL);
    assert(index->column_stats != NULL);

    count = table_index_query(index, table, rows, 3, "tool", matches, 3, err, sizeof(err));
    assert(count == 1);
    assert(matches[0].actual_row == 2);
    assert(matches[0].best_col == 1);
    assert(matches[0].match_start == 0);
    assert(matches[0].match_len == 4);

    table_index_free(index);
    free_table(table);
}

int main(void)
{
    test_build_and_stats();
    test_substring_search_only();
    test_build_without_row_vectors();
    puts("table index tests passed");
    return 0;
}
