/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Core table allocation, teardown, and basic mutation helpers. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data/table.h"

static void *clone_cell_value(DataType type, const void *value) {
    void *copy = NULL;

    if (!value) return NULL;

    switch (type) {
        case TYPE_INT:
        case TYPE_BOOL:
            copy = malloc(sizeof(int));
            if (copy) *(int *)copy = *(const int *)value;
            return copy;
        case TYPE_FLOAT:
            copy = malloc(sizeof(float));
            if (copy) *(float *)copy = *(const float *)value;
            return copy;
        case TYPE_STR:
            return strdup((const char *)value);
        default:
            return NULL;
    }
}

Table *create_table(const char *name) {
    Table *t = malloc(sizeof(Table));
    t->name = strdup(name);
    t->columns = NULL;
    t->rows = NULL;
    t->column_count = 0;
    t->row_count = 0;
    t->capacity_columns = 0;
    t->capacity_rows = 0;
    t->dirty = 0;
    return t;
}

Table *clone_table(const Table *table) {
    Table *copy;

    if (!table) return NULL;

    copy = create_table(table->name ? table->name : "Untitled Table");
    if (!copy) return NULL;

    if (table->column_count > 0) {
        copy->columns = calloc((size_t)table->column_count, sizeof(Column));
        if (!copy->columns) {
            free_table(copy);
            return NULL;
        }
        copy->column_count = table->column_count;
        copy->capacity_columns = table->column_count;
        for (int c = 0; c < table->column_count; ++c) {
            copy->columns[c].name = strdup(table->columns[c].name ? table->columns[c].name : "");
            if (!copy->columns[c].name) {
                free_table(copy);
                return NULL;
            }
            copy->columns[c].type = table->columns[c].type;
            copy->columns[c].color_pair_id = table->columns[c].color_pair_id;
        }
    }

    if (table->row_count > 0) {
        copy->rows = calloc((size_t)table->row_count, sizeof(Row));
        if (!copy->rows) {
            free_table(copy);
            return NULL;
        }
        copy->row_count = table->row_count;
        copy->capacity_rows = table->row_count;
        for (int r = 0; r < table->row_count; ++r) {
            if (table->column_count <= 0) continue;
            copy->rows[r].values = calloc((size_t)table->column_count, sizeof(void *));
            if (!copy->rows[r].values) {
                free_table(copy);
                return NULL;
            }
            for (int c = 0; c < table->column_count; ++c) {
                copy->rows[r].values[c] = clone_cell_value(table->columns[c].type,
                                                            table->rows[r].values ? table->rows[r].values[c] : NULL);
                if (table->rows[r].values && table->rows[r].values[c] && !copy->rows[r].values[c]) {
                    free_table(copy);
                    return NULL;
                }
            }
        }
    }

    copy->dirty = table->dirty;
    return copy;
}

/* Free the table object together with all owned columns, rows, and cells. */
void free_table(Table *t) {
    if (!t) return;

    if (t->name) free(t->name);
    for (int i = 0; i < t->column_count; i++) {
        if (t->columns[i].name) free(t->columns[i].name);
    }
    free(t->columns);

    for (int i = 0; i < t->row_count; i++) {
        if (t->rows[i].values) {
            for (int j = 0; j < t->column_count; j++) {
                if (t->rows[i].values[j]) {
                    free(t->rows[i].values[j]);
                }
            }
            free(t->rows[i].values);
        }
    }

    free(t->rows);
    free(t);
}

/* Reset a reusable table to an empty state with a fresh display name. */
void clear_table(Table *t, const char *name) {
    if (!t) return;

    int old_cols = t->column_count;
    int old_rows = t->row_count;

    if (t->name) free(t->name);
    for (int i = 0; i < old_cols; i++) {
        if (t->columns[i].name) free(t->columns[i].name);
    }
    free(t->columns);

    for (int i = 0; i < old_rows; i++) {
        if (t->rows[i].values) {
            for (int j = 0; j < old_cols; j++) {
                if (t->rows[i].values[j]) free(t->rows[i].values[j]);
            }
            free(t->rows[i].values);
        }
    }
    free(t->rows);

    t->name = strdup(name ? name : "Untitled Table");
    t->columns = NULL;
    t->rows = NULL;
    t->column_count = 0;
    t->row_count = 0;
    t->capacity_columns = 0;
    t->capacity_rows = 0;
    t->dirty = 0;
}

/* Move all owned storage from src into dest, freeing the old dest contents. */
int replace_table_contents(Table *dest, Table *src) {
    if (!dest || !src) return -1;

    clear_table(dest, src->name ? src->name : "Untitled Table");
    free(dest->name);
    dest->name = src->name;
    dest->columns = src->columns;
    dest->column_count = src->column_count;
    dest->rows = src->rows;
    dest->row_count = src->row_count;
    dest->capacity_columns = src->capacity_columns;
    dest->capacity_rows = src->capacity_rows;
    dest->dirty = src->dirty;

    src->name = NULL;
    src->columns = NULL;
    src->rows = NULL;
    src->column_count = 0;
    src->row_count = 0;
    src->capacity_columns = 0;
    src->capacity_rows = 0;
    src->dirty = 0;
    free(src);
    return 0;
}

/* Append a new column and extend all existing rows with an empty cell slot. */
int add_column(Table *t, const char *name, DataType type) {
    if (t->column_count == t->capacity_columns) {
        t->capacity_columns += 4;
        t->columns = realloc(t->columns, t->capacity_columns * sizeof(Column));
    }

    t->columns[t->column_count].name = strdup(name);
    t->columns[t->column_count].type = type;

    // 🌈 Assign rainbow color
    int color_cycle[] = {10, 11, 12, 13, 14, 15, 16};
    int color_count = sizeof(color_cycle) / sizeof(color_cycle[0]);
    t->columns[t->column_count].color_pair_id = color_cycle[t->column_count % color_count];

    t->column_count++;

    for (int i = 0; i < t->row_count; i++) {
        t->rows[i].values = realloc(t->rows[i].values, t->column_count * sizeof(void *));
        t->rows[i].values[t->column_count - 1] = NULL;
    }

    return 0;
}


/* Append a new row by converting each input string to the column's data type. */
int add_row(Table *t, const char **input_strings) {
    if (t->row_count == t->capacity_rows) {
        t->capacity_rows += 4;
        t->rows = realloc(t->rows, t->capacity_rows * sizeof(Row));
    }

    Row *r = &t->rows[t->row_count];
    r->values = malloc(t->column_count * sizeof(void *));

    for (int i = 0; i < t->column_count; i++) {
        const char *in = input_strings[i];
        if (t->columns[i].type == TYPE_INT) {
            int *v = malloc(sizeof(int));
            *v = atoi(in);
            r->values[i] = v;
        } else if (t->columns[i].type == TYPE_FLOAT) {
            float *v = malloc(sizeof(float));
            *v = strtof(in, NULL);
            r->values[i] = v;
        } else if (t->columns[i].type == TYPE_BOOL) {
            int *v = malloc(sizeof(int));
            *v = (strcmp(in, "true") == 0);
            r->values[i] = v;
        } else {
            r->values[i] = strdup(in);
        }
    }

    t->row_count++;
    return 0;
}

const char *type_to_string(DataType type) {
    switch (type) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STR: return "str";
        case TYPE_BOOL: return "bool";
        default: return "unknown";
    }
}

/* Parse a persisted or user-entered type name into the internal enum. */
DataType parse_type_from_string(const char *str) {
    if (strcmp(str, "int") == 0) return TYPE_INT;
    if (strcmp(str, "float") == 0) return TYPE_FLOAT;
    if (strcmp(str, "str") == 0) return TYPE_STR;
    if (strcmp(str, "bool") == 0) return TYPE_BOOL;
    return TYPE_UNKNOWN;
}
