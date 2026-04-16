/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Core table data structures and ownership-oriented helpers. */

#ifndef TTB2_TABLE_H
#define TTB2_TABLE_H

/* Supported logical data types for table columns and cell parsing. */
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_BOOL,
    TYPE_UNKNOWN
} DataType;

/* Column metadata for a single table field. */
typedef struct {
    char *name;
    DataType type;
    int color_pair_id;
} Column;

/* Heap-owned cell storage for a single row. */
typedef struct {
    void **values;
} Row;

/* In-memory table model shared by importers, editors, and exporters. */
typedef struct {
    char *name;
    Column *columns;
    int column_count;

    Row *rows;
    int row_count;

    int capacity_columns;
    int capacity_rows;
    int dirty;
} Table;

/* Table lifecycle helpers. */
Table *create_table(const char *name);
/* Deep-clone a table including all cells, preserving nulls and metadata. */
Table *clone_table(const Table *table);
void free_table(Table *table);
/* Clear all rows and columns while keeping the table object reusable. */
void clear_table(Table *table, const char *name);
/* Replace the destination contents with src data, transferring ownership. */
int replace_table_contents(Table *dest, Table *src);

/* Structural mutation helpers used by loaders and editing flows. */
int add_column(Table *table, const char *name, DataType type);
/* Append a row using caller-supplied string representations for each cell. */
int add_row(Table *table, const char **input_strings);

/* Convert between textual and enum type representations. */
const char *type_to_string(DataType type);
/* Parse a user- or file-provided type label into the corresponding enum. */
DataType parse_type_from_string(const char *str);

#endif
