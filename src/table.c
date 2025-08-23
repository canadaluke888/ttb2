#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/tablecraft.h"

Table *create_table(const char *name) {
    Table *t = malloc(sizeof(Table));
    t->name = strdup(name);
    t->columns = NULL;
    t->rows = NULL;
    t->column_count = 0;
    t->row_count = 0;
    t->capacity_columns = 0;
    t->capacity_rows = 0;
    return t;
}

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

DataType parse_type_from_string(const char *str) {
    if (strcmp(str, "int") == 0) return TYPE_INT;
    if (strcmp(str, "float") == 0) return TYPE_FLOAT;
    if (strcmp(str, "str") == 0) return TYPE_STR;
    if (strcmp(str, "bool") == 0) return TYPE_BOOL;
    return TYPE_UNKNOWN;
}
