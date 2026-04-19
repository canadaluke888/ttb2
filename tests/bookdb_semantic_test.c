/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Persistence checks for semantic index save/load and stale invalidation. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data/table.h"
#include "data/table_ops.h"
#include "db/book_db.h"
#include "vector/table_index.h"

static Table *make_table(const char *name, const char *row0_a, const char *row0_b, const char *row1_a, const char *row1_b)
{
    Table *table = create_table(name);
    char err[256] = {0};
    const char *row0[] = {row0_a, row0_b};
    const char *row1[] = {row1_a, row1_b};

    assert(table != NULL);
    assert(tableop_insert_column(table, "label", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "group_name", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row1, err, sizeof(err)) == 0);
    return table;
}

static void test_bookdb_semantic_round_trip(void)
{
    char path[] = "/tmp/ttb2_semantic_testXXXXXX";
    int fd = mkstemp(path);
    BookDB *db;
    Table *table;
    Table *other;
    TableIndex *index = NULL;
    TableIndex *loaded = NULL;
    char table_id[256] = {0};
    char other_id[256] = {0};
    char err[256] = {0};

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(bookdb_create_empty(path, "Semantic Test", err, sizeof(err)) == 0);
    db = bookdb_open(path, 0, err, sizeof(err));
    assert(db != NULL);
    assert(bookdb_init_schema(db, err, sizeof(err)) == 0);

    table = make_table("Alpha", "apple", "fruit", "hammer", "tools");
    other = make_table("Beta", "orange", "fruit", "saw", "tools");

    assert(bookdb_create_table(db, table, table_id, sizeof(table_id), err, sizeof(err)) == 0);
    assert(bookdb_create_table(db, other, other_id, sizeof(other_id), err, sizeof(err)) == 0);
    assert(table_index_build_for_table(table, NULL, &index, err, sizeof(err)) == 0);
    assert(bookdb_save_semantic_index(db, table_id, index, err, sizeof(err)) == 0);

    loaded = bookdb_load_semantic_index(db, table_id, err, sizeof(err));
    assert(loaded != NULL);
    assert(loaded->stale == 0);
    assert(loaded->fingerprint == index->fingerprint);
    assert(loaded->row_count == index->row_count);
    assert(loaded->column_count == index->column_count);

    table_index_free(loaded);
    loaded = NULL;

    assert(bookdb_save_table_incremental(db, table_id, table, table, err, sizeof(err)) == 0);
    loaded = bookdb_load_semantic_index(db, table_id, err, sizeof(err));
    assert(loaded != NULL);
    assert(loaded->stale == 1);

    table_index_free(loaded);
    loaded = NULL;

    assert(bookdb_mark_semantic_index_stale(db, other_id, err, sizeof(err)) == 0);
    assert(bookdb_delete_semantic_index(db, table_id, err, sizeof(err)) == 0);
    loaded = bookdb_load_semantic_index(db, table_id, err, sizeof(err));
    assert(loaded == NULL);

    table_index_free(index);
    free_table(table);
    free_table(other);
    bookdb_close(db);
    unlink(path);
}

int main(void)
{
    test_bookdb_semantic_round_trip();
    puts("bookdb semantic tests passed");
    return 0;
}
