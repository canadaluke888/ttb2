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

static void test_hybrid_ranking(void)
{
    Table *table = make_sample_table();
    TableIndex *index = NULL;
    TableIndexMatch matches[3];
    int rows[] = {0, 1, 2};
    char err[256] = {0};
    int count;

    assert(table_index_build_for_table(table, NULL, &index, err, sizeof(err)) == 0);

    count = table_index_query(index, table, rows, 3, "apple fruit", matches, 3, err, sizeof(err));
    assert(count > 0);
    assert(matches[0].actual_row == 0);
    assert(matches[0].best_col == 0 || matches[0].best_col == 1);
    assert(matches[0].lexical_score > 0.0f);

    count = table_index_query(index, table, rows, 3, "hammer", matches, 3, err, sizeof(err));
    assert(count > 0);
    assert(matches[0].actual_row == 2);
    assert(matches[0].score >= matches[0].semantic_score);

    table_index_free(index);
    free_table(table);
}

int main(void)
{
    test_build_and_stats();
    test_hybrid_ranking();
    puts("table index tests passed");
    return 0;
}
