#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "table_ops.h"

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int valid_index(const Table *table, int row, int col)
{
    return table &&
           row >= 0 && row < table->row_count &&
           col >= 0 && col < table->column_count;
}

static int ensure_column_capacity(Table *table)
{
    Column *cols;

    if (table->column_count < table->capacity_columns) return 0;
    cols = realloc(table->columns, (size_t)(table->capacity_columns + 4) * sizeof(Column));
    if (!cols) return -1;
    table->columns = cols;
    table->capacity_columns += 4;
    return 0;
}

static int ensure_row_capacity(Table *table)
{
    Row *rows;

    if (table->row_count < table->capacity_rows) return 0;
    rows = realloc(table->rows, (size_t)(table->capacity_rows + 4) * sizeof(Row));
    if (!rows) return -1;
    table->rows = rows;
    table->capacity_rows += 4;
    return 0;
}

static void *default_value_for_type(DataType type)
{
    switch (type) {
        case TYPE_INT: {
            int *v = malloc(sizeof(int));
            if (v) *v = 0;
            return v;
        }
        case TYPE_FLOAT: {
            float *v = malloc(sizeof(float));
            if (v) *v = 0.0f;
            return v;
        }
        case TYPE_BOOL: {
            int *v = malloc(sizeof(int));
            if (v) *v = 0;
            return v;
        }
        case TYPE_STR:
            return strdup("");
        default:
            return NULL;
    }
}

static int parse_bool(const char *input, int *out)
{
    if (!input || !out) return -1;
    if (strcasecmp(input, "true") == 0 || strcmp(input, "1") == 0) {
        *out = 1;
        return 0;
    }
    if (strcasecmp(input, "false") == 0 || strcmp(input, "0") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int parse_value(DataType type, const char *input, void **out)
{
    char *endptr = NULL;

    if (!out) return -1;
    *out = NULL;

    if (type == TYPE_STR) {
        *out = strdup(input ? input : "");
        return *out ? 0 : -1;
    }

    if (!input || !*input) return -1;

    switch (type) {
        case TYPE_INT: {
            long parsed;
            int *v = malloc(sizeof(int));
            if (!v) return -1;
            parsed = strtol(input, &endptr, 10);
            if (*endptr != '\0') {
                free(v);
                return -1;
            }
            *v = (int)parsed;
            *out = v;
            return 0;
        }
        case TYPE_FLOAT: {
            float *v = malloc(sizeof(float));
            if (!v) return -1;
            *v = strtof(input, &endptr);
            if (*endptr != '\0') {
                free(v);
                return -1;
            }
            *out = v;
            return 0;
        }
        case TYPE_BOOL: {
            int *v = malloc(sizeof(int));
            if (!v) return -1;
            if (parse_bool(input, v) != 0) {
                free(v);
                return -1;
            }
            *out = v;
            return 0;
        }
        default:
            return -1;
    }
}

static int cell_to_string(const Table *table, int row, int col, char *buf, size_t buf_sz)
{
    void *v;

    if (!valid_index(table, row, col) || !buf || buf_sz == 0) return -1;
    v = table->rows[row].values ? table->rows[row].values[col] : NULL;
    if (!v) {
        buf[0] = '\0';
        return 0;
    }

    switch (table->columns[col].type) {
        case TYPE_INT:
            snprintf(buf, buf_sz, "%d", *(int *)v);
            break;
        case TYPE_FLOAT:
            snprintf(buf, buf_sz, "%g", *(float *)v);
            break;
        case TYPE_BOOL:
            snprintf(buf, buf_sz, "%s", (*(int *)v) ? "true" : "false");
            break;
        case TYPE_STR:
            snprintf(buf, buf_sz, "%s", (char *)v);
            break;
        default:
            return -1;
    }
    return 0;
}

static void assign_column_color(Table *table, int index)
{
    static const int color_cycle[] = {10, 11, 12, 13, 14, 15, 16};
    int color_count = (int)(sizeof(color_cycle) / sizeof(color_cycle[0]));
    table->columns[index].color_pair_id = color_cycle[index % color_count];
}

int tableop_set_cell(Table *table, int row, int col, const char *input, char *err, size_t err_sz)
{
    void *parsed = NULL;

    if (!valid_index(table, row, col)) {
        set_err(err, err_sz, "Invalid cell");
        return -1;
    }
    if (parse_value(table->columns[col].type, input, &parsed) != 0) {
        set_err(err, err_sz, "Invalid input for column type");
        return -1;
    }

    free(table->rows[row].values[col]);
    table->rows[row].values[col] = parsed;
    table->dirty = 1;
    return 0;
}

int tableop_clear_cell(Table *table, int row, int col, char *err, size_t err_sz)
{
    if (!valid_index(table, row, col)) {
        set_err(err, err_sz, "Invalid cell");
        return -1;
    }
    free(table->rows[row].values[col]);
    table->rows[row].values[col] = NULL;
    table->dirty = 1;
    return 0;
}

int tableop_delete_row(Table *table, int row, char *err, size_t err_sz)
{
    if (!table || row < 0 || row >= table->row_count) {
        set_err(err, err_sz, "Invalid row");
        return -1;
    }
    if (table->rows[row].values) {
        for (int c = 0; c < table->column_count; ++c) {
            free(table->rows[row].values[c]);
        }
        free(table->rows[row].values);
    }
    if (row < table->row_count - 1) {
        memmove(&table->rows[row], &table->rows[row + 1], (size_t)(table->row_count - row - 1) * sizeof(Row));
    }
    table->row_count--;
    table->dirty = 1;
    return 0;
}

int tableop_delete_column(Table *table, int col, char *err, size_t err_sz)
{
    if (!table || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (table->column_count <= 1) {
        set_err(err, err_sz, "Cannot delete the last column");
        return -1;
    }

    free(table->columns[col].name);
    if (col < table->column_count - 1) {
        memmove(&table->columns[col], &table->columns[col + 1], (size_t)(table->column_count - col - 1) * sizeof(Column));
    }
    table->column_count--;

    for (int c = col; c < table->column_count; ++c) {
        assign_column_color(table, c);
    }

    for (int r = 0; r < table->row_count; ++r) {
        if (!table->rows[r].values) continue;
        free(table->rows[r].values[col]);
        if (col < table->column_count) {
            memmove(&table->rows[r].values[col],
                    &table->rows[r].values[col + 1],
                    (size_t)(table->column_count - col) * sizeof(void *));
        }
        if (table->column_count > 0) {
            void **values = realloc(table->rows[r].values, (size_t)table->column_count * sizeof(void *));
            if (values) table->rows[r].values = values;
        } else {
            free(table->rows[r].values);
            table->rows[r].values = NULL;
        }
    }

    table->dirty = 1;
    return 0;
}

int tableop_insert_row(Table *table, const char **values, char *err, size_t err_sz)
{
    Row *row;

    if (!table) {
        set_err(err, err_sz, "No table");
        return -1;
    }
    if (table->column_count <= 0) {
        set_err(err, err_sz, "Add at least one column first");
        return -1;
    }
    if (ensure_row_capacity(table) != 0) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    row = &table->rows[table->row_count];
    row->values = calloc((size_t)table->column_count, sizeof(void *));
    if (!row->values) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    for (int c = 0; c < table->column_count; ++c) {
        if (parse_value(table->columns[c].type, values ? values[c] : "", &row->values[c]) != 0) {
            for (int i = 0; i < c; ++i) free(row->values[i]);
            free(row->values);
            row->values = NULL;
            set_err(err, err_sz, "Invalid row value");
            return -1;
        }
    }

    table->row_count++;
    table->dirty = 1;
    return 0;
}

int tableop_insert_column(Table *table, const char *name, DataType type, char *err, size_t err_sz)
{
    char *name_copy;

    if (!table) {
        set_err(err, err_sz, "No table");
        return -1;
    }
    if (!name || !*name) {
        set_err(err, err_sz, "Column name is required");
        return -1;
    }
    if (type == TYPE_UNKNOWN) {
        set_err(err, err_sz, "Invalid column type");
        return -1;
    }
    if (ensure_column_capacity(table) != 0) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    name_copy = strdup(name);
    if (!name_copy) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    table->columns[table->column_count].name = name_copy;
    table->columns[table->column_count].type = type;
    assign_column_color(table, table->column_count);
    table->column_count++;

    for (int r = 0; r < table->row_count; ++r) {
        void **values = realloc(table->rows[r].values, (size_t)table->column_count * sizeof(void *));
        if (!values) {
            table->column_count--;
            free(table->columns[table->column_count].name);
            table->columns[table->column_count].name = NULL;
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
        table->rows[r].values = values;
        table->rows[r].values[table->column_count - 1] = NULL;
    }

    table->dirty = 1;
    return 0;
}

int tableop_rename_column(Table *table, int col, const char *name, char *err, size_t err_sz)
{
    char *copy;

    if (!table || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (!name || !*name) {
        set_err(err, err_sz, "Column name is required");
        return -1;
    }

    copy = strdup(name);
    if (!copy) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    free(table->columns[col].name);
    table->columns[col].name = copy;
    table->dirty = 1;
    return 0;
}

int tableop_change_column_type(Table *table, int col, DataType type, char *err, size_t err_sz)
{
    void **new_values;

    if (!table || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (type == TYPE_UNKNOWN) {
        set_err(err, err_sz, "Invalid column type");
        return -1;
    }

    new_values = calloc((size_t)table->row_count, sizeof(void *));
    if (table->row_count > 0 && !new_values) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    for (int r = 0; r < table->row_count; ++r) {
        char buf[128];

        if (cell_to_string(table, r, col, buf, sizeof(buf)) != 0) {
            free(new_values);
            set_err(err, err_sz, "Failed to convert existing values");
            return -1;
        }

        if (buf[0] == '\0') {
            new_values[r] = default_value_for_type(type);
        } else if (parse_value(type, buf, &new_values[r]) != 0) {
            new_values[r] = default_value_for_type(type);
        }

        if (!new_values[r]) {
            for (int i = 0; i < r; ++i) free(new_values[i]);
            free(new_values);
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
    }

    for (int r = 0; r < table->row_count; ++r) {
        free(table->rows[r].values[col]);
        table->rows[r].values[col] = new_values[r];
    }
    free(new_values);

    table->columns[col].type = type;
    table->dirty = 1;
    return 0;
}
