/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Undo and redo implementation for table edits. */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "ui/ui_history.h"
#include "db/db_manager.h"
#include "data/table_ops.h"
#include "ui/internal.h"
#include "core/workspace.h"

#define UI_HISTORY_LIMIT 128

typedef enum {
    UIH_ACTION_CELL_SET = 0,
    UIH_ACTION_CELL_CLEAR,
    UIH_ACTION_ROW_INSERT,
    UIH_ACTION_ROW_DELETE,
    UIH_ACTION_ROW_MOVE,
    UIH_ACTION_ROW_SWAP,
    UIH_ACTION_COL_INSERT,
    UIH_ACTION_COL_DELETE,
    UIH_ACTION_COL_MOVE,
    UIH_ACTION_COL_SWAP,
    UIH_ACTION_COL_RENAME,
    UIH_ACTION_COL_TYPE
} UiHistoryActionKind;

typedef struct {
    int is_null;
    char *text;
} UiHistoryValue;

typedef struct {
    UiHistoryValue *values;
    int count;
} UiHistoryRowSnapshot;

typedef struct {
    char *name;
    DataType type;
    int color_pair_id;
    UiHistoryValue *values;
    int row_count;
} UiHistoryColumnSnapshot;

typedef struct {
    int row;
    int col;
    UiHistoryValue before;
    UiHistoryValue after;
} UiHistoryCellAction;

typedef struct {
    int index;
    UiHistoryRowSnapshot row;
} UiHistoryRowInsertDeleteAction;

typedef struct {
    int from_index;
    int to_index;
} UiHistoryMoveAction;

typedef struct {
    int index_a;
    int index_b;
} UiHistorySwapAction;

typedef struct {
    int index;
    UiHistoryColumnSnapshot column;
} UiHistoryColumnInsertDeleteAction;

typedef struct {
    int col;
    char *before_name;
    char *after_name;
} UiHistoryRenameAction;

typedef struct {
    int col;
    UiHistoryColumnSnapshot before;
    UiHistoryColumnSnapshot after;
} UiHistoryColumnTypeAction;

typedef struct {
    UiHistoryActionKind kind;
    int focus_actual_row;
    int focus_col;
    int focus_header;
    union {
        UiHistoryCellAction cell;
        UiHistoryRowInsertDeleteAction row_insert;
        UiHistoryRowInsertDeleteAction row_delete;
        UiHistoryMoveAction row_move;
        UiHistorySwapAction row_swap;
        UiHistoryColumnInsertDeleteAction col_insert;
        UiHistoryColumnInsertDeleteAction col_delete;
        UiHistoryMoveAction col_move;
        UiHistorySwapAction col_swap;
        UiHistoryRenameAction col_rename;
        UiHistoryColumnTypeAction col_type;
    } data;
} UiHistoryEntry;

static UiHistoryEntry g_undo[UI_HISTORY_LIMIT];
static UiHistoryEntry g_redo[UI_HISTORY_LIMIT];
static int g_undo_count = 0;
static int g_redo_count = 0;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) return;
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static char *dup_or_empty(const char *text)
{
    char *copy = strdup(text ? text : "");
    return copy;
}

static void free_history_value(UiHistoryValue *value)
{
    if (!value) return;
    free(value->text);
    value->text = NULL;
    value->is_null = 1;
}

static void free_row_snapshot(UiHistoryRowSnapshot *row)
{
    if (!row) return;
    for (int i = 0; i < row->count; ++i) {
        free_history_value(&row->values[i]);
    }
    free(row->values);
    row->values = NULL;
    row->count = 0;
}

static void free_column_snapshot(UiHistoryColumnSnapshot *col)
{
    if (!col) return;
    free(col->name);
    col->name = NULL;
    for (int i = 0; i < col->row_count; ++i) {
        free_history_value(&col->values[i]);
    }
    free(col->values);
    col->values = NULL;
    col->row_count = 0;
}

static void free_entry(UiHistoryEntry *entry)
{
    if (!entry) return;
    switch (entry->kind) {
        case UIH_ACTION_CELL_SET:
        case UIH_ACTION_CELL_CLEAR:
            free_history_value(&entry->data.cell.before);
            free_history_value(&entry->data.cell.after);
            break;
        case UIH_ACTION_ROW_INSERT:
            free_row_snapshot(&entry->data.row_insert.row);
            break;
        case UIH_ACTION_ROW_DELETE:
            free_row_snapshot(&entry->data.row_delete.row);
            break;
        case UIH_ACTION_COL_INSERT:
            free_column_snapshot(&entry->data.col_insert.column);
            break;
        case UIH_ACTION_COL_DELETE:
            free_column_snapshot(&entry->data.col_delete.column);
            break;
        case UIH_ACTION_COL_RENAME:
            free(entry->data.col_rename.before_name);
            free(entry->data.col_rename.after_name);
            entry->data.col_rename.before_name = NULL;
            entry->data.col_rename.after_name = NULL;
            break;
        case UIH_ACTION_COL_TYPE:
            free_column_snapshot(&entry->data.col_type.before);
            free_column_snapshot(&entry->data.col_type.after);
            break;
        default:
            break;
    }
    memset(entry, 0, sizeof(*entry));
}

static void clear_stack(UiHistoryEntry *stack, int *count)
{
    if (!stack || !count) return;
    for (int i = 0; i < *count; ++i) {
        free_entry(&stack[i]);
    }
    *count = 0;
}

static int capture_cell_value(const Table *table, int row, int col, UiHistoryValue *out)
{
    void *value;
    char buf[128];

    if (!table || !out || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    value = table->rows[row].values ? table->rows[row].values[col] : NULL;
    if (!value) {
        out->is_null = 1;
        return 0;
    }

    switch (table->columns[col].type) {
        case TYPE_INT:
            snprintf(buf, sizeof(buf), "%d", *(int *)value);
            break;
        case TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%g", *(float *)value);
            break;
        case TYPE_BOOL:
            snprintf(buf, sizeof(buf), "%s", (*(int *)value) ? "true" : "false");
            break;
        case TYPE_STR:
            snprintf(buf, sizeof(buf), "%s", (char *)value);
            break;
        default:
            return -1;
    }

    out->text = strdup(buf);
    if (!out->text) return -1;
    out->is_null = 0;
    return 0;
}

static int snapshot_row(const Table *table, int row, UiHistoryRowSnapshot *out)
{
    if (!table || !out || row < 0 || row >= table->row_count) return -1;
    memset(out, 0, sizeof(*out));
    out->count = table->column_count;
    if (out->count <= 0) return 0;
    out->values = calloc((size_t)out->count, sizeof(*out->values));
    if (!out->values) return -1;
    for (int col = 0; col < out->count; ++col) {
        if (capture_cell_value(table, row, col, &out->values[col]) != 0) {
            free_row_snapshot(out);
            return -1;
        }
    }
    return 0;
}

static int snapshot_column(const Table *table, int col, UiHistoryColumnSnapshot *out)
{
    if (!table || !out || col < 0 || col >= table->column_count) return -1;
    memset(out, 0, sizeof(*out));
    out->name = dup_or_empty(table->columns[col].name);
    if (!out->name) return -1;
    out->type = table->columns[col].type;
    out->color_pair_id = table->columns[col].color_pair_id;
    out->row_count = table->row_count;
    if (out->row_count <= 0) return 0;
    out->values = calloc((size_t)out->row_count, sizeof(*out->values));
    if (!out->values) {
        free_column_snapshot(out);
        return -1;
    }
    for (int row = 0; row < out->row_count; ++row) {
        if (capture_cell_value(table, row, col, &out->values[row]) != 0) {
            free_column_snapshot(out);
            return -1;
        }
    }
    return 0;
}

static int parse_bool_value(const char *input, int *out)
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

static int parse_value_for_type(DataType type, const char *input, void **out)
{
    char *endptr = NULL;

    if (!out) return -1;
    *out = NULL;

    if (type == TYPE_STR) {
        *out = dup_or_empty(input);
        return *out ? 0 : -1;
    }

    if (!input || !*input) return -1;

    switch (type) {
        case TYPE_INT: {
            long parsed;
            int *value = malloc(sizeof(int));
            if (!value) return -1;
            parsed = strtol(input, &endptr, 10);
            if (*endptr != '\0') {
                free(value);
                return -1;
            }
            *value = (int)parsed;
            *out = value;
            return 0;
        }
        case TYPE_FLOAT: {
            float *value = malloc(sizeof(float));
            if (!value) return -1;
            *value = strtof(input, &endptr);
            if (*endptr != '\0') {
                free(value);
                return -1;
            }
            *out = value;
            return 0;
        }
        case TYPE_BOOL: {
            int *value = malloc(sizeof(int));
            if (!value) return -1;
            if (parse_bool_value(input, value) != 0) {
                free(value);
                return -1;
            }
            *out = value;
            return 0;
        }
        default:
            return -1;
    }
}

static int set_raw_cell(Table *table, int row, int col, DataType type, const UiHistoryValue *value, char *err, size_t err_sz)
{
    void *parsed = NULL;

    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid cell");
        return -1;
    }

    if (!value->is_null && parse_value_for_type(type, value->text, &parsed) != 0) {
        set_err(err, err_sz, "Failed to parse stored cell value");
        return -1;
    }

    free(table->rows[row].values[col]);
    table->rows[row].values[col] = parsed;
    table->dirty = 1;
    return 0;
}

static int apply_cell_value(Table *table, int row, int col, const UiHistoryValue *value, char *err, size_t err_sz)
{
    if (!value) {
        set_err(err, err_sz, "Missing cell value");
        return -1;
    }
    return set_raw_cell(table, row, col, table->columns[col].type, value, err, err_sz);
}

static int ensure_row_capacity(Table *table)
{
    Row *rows;

    if (!table) return -1;
    if (table->row_count < table->capacity_rows) return 0;
    rows = realloc(table->rows, (size_t)(table->capacity_rows + 4) * sizeof(Row));
    if (!rows) return -1;
    table->rows = rows;
    table->capacity_rows += 4;
    return 0;
}

static void assign_column_color(Table *table, int index)
{
    static const int color_cycle[] = {10, 11, 12, 13, 14, 15, 16};
    int color_count = (int)(sizeof(color_cycle) / sizeof(color_cycle[0]));

    if (!table || index < 0 || index >= table->column_count) return;
    table->columns[index].color_pair_id = color_cycle[index % color_count];
}

static void reassign_column_colors(Table *table)
{
    if (!table) return;
    for (int i = 0; i < table->column_count; ++i) {
        assign_column_color(table, i);
    }
}

static int insert_row_snapshot(Table *table, int row_index, const UiHistoryRowSnapshot *snapshot, char *err, size_t err_sz)
{
    Row row = {0};

    if (!table || !snapshot || row_index < 0 || row_index > table->row_count) {
        set_err(err, err_sz, "Invalid row index");
        return -1;
    }
    if (snapshot->count != table->column_count) {
        set_err(err, err_sz, "Row snapshot does not match table schema");
        return -1;
    }
    if (ensure_row_capacity(table) != 0) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    row.values = calloc((size_t)table->column_count, sizeof(void *));
    if (!row.values) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    if (row_index < table->row_count) {
        memmove(&table->rows[row_index + 1],
                &table->rows[row_index],
                (size_t)(table->row_count - row_index) * sizeof(Row));
    }
    table->rows[row_index] = row;
    table->row_count++;
    table->dirty = 1;

    for (int col = 0; col < snapshot->count; ++col) {
        if (apply_cell_value(table, row_index, col, &snapshot->values[col], err, err_sz) != 0) {
            return -1;
        }
    }

    return 0;
}

static int restore_column_cells(Table *table, int col, const UiHistoryColumnSnapshot *snapshot, char *err, size_t err_sz)
{
    if (!table || !snapshot || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (snapshot->row_count != table->row_count) {
        set_err(err, err_sz, "Column snapshot row count mismatch");
        return -1;
    }
    table->columns[col].type = snapshot->type;
    for (int row = 0; row < table->row_count; ++row) {
        if (set_raw_cell(table, row, col, snapshot->type, &snapshot->values[row], err, err_sz) != 0) {
            return -1;
        }
    }
    return 0;
}

static int insert_column_snapshot(Table *table, int col_index, const UiHistoryColumnSnapshot *snapshot, char *err, size_t err_sz)
{
    if (!table || !snapshot) {
        set_err(err, err_sz, "Missing column snapshot");
        return -1;
    }
    if (tableop_insert_column_at(table, col_index, snapshot->name, snapshot->type, err, err_sz) != 0) {
        return -1;
    }
    table->columns[col_index].color_pair_id = snapshot->color_pair_id;
    if (restore_column_cells(table, col_index, snapshot, err, err_sz) != 0) {
        return -1;
    }
    reassign_column_colors(table);
    return 0;
}

static int move_row_exact(Table *table, int from_index, int to_index, char *err, size_t err_sz)
{
    Row moved;

    if (!table || from_index < 0 || from_index >= table->row_count || to_index < 0 || to_index >= table->row_count) {
        set_err(err, err_sz, "Invalid row");
        return -1;
    }
    if (from_index == to_index) return 0;

    moved = table->rows[from_index];
    if (from_index < to_index) {
        memmove(&table->rows[from_index],
                &table->rows[from_index + 1],
                (size_t)(to_index - from_index) * sizeof(Row));
    } else {
        memmove(&table->rows[to_index + 1],
                &table->rows[to_index],
                (size_t)(from_index - to_index) * sizeof(Row));
    }
    table->rows[to_index] = moved;
    table->dirty = 1;
    return 0;
}

static int move_column_exact(Table *table, int from_index, int to_index, char *err, size_t err_sz)
{
    Column moved;

    if (!table || from_index < 0 || from_index >= table->column_count || to_index < 0 || to_index >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (from_index == to_index) return 0;

    moved = table->columns[from_index];
    if (from_index < to_index) {
        memmove(&table->columns[from_index],
                &table->columns[from_index + 1],
                (size_t)(to_index - from_index) * sizeof(Column));
    } else {
        memmove(&table->columns[to_index + 1],
                &table->columns[to_index],
                (size_t)(from_index - to_index) * sizeof(Column));
    }
    table->columns[to_index] = moved;

    for (int row = 0; row < table->row_count; ++row) {
        void *value;

        if (!table->rows[row].values) continue;
        value = table->rows[row].values[from_index];
        if (from_index < to_index) {
            memmove(&table->rows[row].values[from_index],
                    &table->rows[row].values[from_index + 1],
                    (size_t)(to_index - from_index) * sizeof(void *));
        } else {
            memmove(&table->rows[row].values[to_index + 1],
                    &table->rows[row].values[to_index],
                    (size_t)(from_index - to_index) * sizeof(void *));
        }
        table->rows[row].values[to_index] = value;
    }

    reassign_column_colors(table);
    table->dirty = 1;
    return 0;
}

static int apply_column_snapshot_in_place(Table *table, int col, const UiHistoryColumnSnapshot *snapshot, char *err, size_t err_sz)
{
    char *name_copy;

    if (!table || !snapshot || col < 0 || col >= table->column_count) {
        set_err(err, err_sz, "Invalid column");
        return -1;
    }
    if (snapshot->row_count != table->row_count) {
        set_err(err, err_sz, "Column snapshot row count mismatch");
        return -1;
    }

    name_copy = dup_or_empty(snapshot->name);
    if (!name_copy) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    free(table->columns[col].name);
    table->columns[col].name = name_copy;
    table->columns[col].type = snapshot->type;
    table->columns[col].color_pair_id = snapshot->color_pair_id;
    if (restore_column_cells(table, col, snapshot, err, err_sz) != 0) {
        return -1;
    }
    table->dirty = 1;
    return 0;
}

static void fill_result(UiHistoryApplyResult *result, int actual_row, int col, int header)
{
    if (!result) return;
    result->ok = 1;
    result->focus_actual_row = actual_row;
    result->focus_col = col;
    result->focus_header = header;
}

static int autosave_after_history(Table *table, char *err, size_t err_sz)
{
    (void)err;
    (void)err_sz;
    workspace_queue_autosave(table);
    return 0;
}

static void discard_oldest(UiHistoryEntry *stack, int *count)
{
    if (!stack || !count || *count <= 0) return;
    free_entry(&stack[0]);
    if (*count > 1) {
        memmove(&stack[0], &stack[1], (size_t)(*count - 1) * sizeof(stack[0]));
    }
    (*count)--;
}

static int push_undo(UiHistoryEntry *entry, char *err, size_t err_sz)
{
    if (!entry) return -1;
    clear_stack(g_redo, &g_redo_count);
    if (g_undo_count >= UI_HISTORY_LIMIT) {
        discard_oldest(g_undo, &g_undo_count);
    }
    g_undo[g_undo_count++] = *entry;
    memset(entry, 0, sizeof(*entry));
    return 0;
}

static int push_redo(UiHistoryEntry *entry)
{
    if (!entry) return -1;
    if (g_redo_count >= UI_HISTORY_LIMIT) {
        discard_oldest(g_redo, &g_redo_count);
    }
    g_redo[g_redo_count++] = *entry;
    memset(entry, 0, sizeof(*entry));
    return 0;
}

static int final_index_for_move(int src_index, int dst_index, int place_after)
{
    int insert_index = dst_index + (place_after ? 1 : 0);
    if (src_index < dst_index) insert_index--;
    return insert_index;
}

void ui_history_reset(void)
{
    clear_stack(g_undo, &g_undo_count);
    clear_stack(g_redo, &g_redo_count);
}

int ui_history_can_undo(void)
{
    return g_undo_count > 0;
}

int ui_history_can_redo(void)
{
    return g_redo_count > 0;
}

int ui_history_set_cell(Table *table, int row, int col, const char *input, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_CELL_SET;
    entry.focus_actual_row = row;
    entry.focus_col = col;
    if (capture_cell_value(table, row, col, &entry.data.cell.before) != 0) {
        set_err(err, err_sz, "Failed to capture previous cell value");
        free_entry(&entry);
        return -1;
    }
    if (tableop_set_cell(table, row, col, input, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    if (capture_cell_value(table, row, col, &entry.data.cell.after) != 0) {
        set_err(err, err_sz, "Failed to capture updated cell value");
        free_entry(&entry);
        return -1;
    }
    entry.data.cell.row = row;
    entry.data.cell.col = col;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, row, col, 0);
    return 0;
}

int ui_history_clear_cell(Table *table, int row, int col, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_CELL_CLEAR;
    entry.focus_actual_row = row;
    entry.focus_col = col;
    if (capture_cell_value(table, row, col, &entry.data.cell.before) != 0) {
        set_err(err, err_sz, "Failed to capture previous cell value");
        free_entry(&entry);
        return -1;
    }
    if (tableop_clear_cell(table, row, col, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    entry.data.cell.after.is_null = 1;
    entry.data.cell.row = row;
    entry.data.cell.col = col;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, row, col, 0);
    return 0;
}

int ui_history_insert_row(Table *table, int row_index, const char **values, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_ROW_INSERT;
    entry.focus_actual_row = row_index;
    entry.focus_col = 0;
    if (tableop_insert_row_at(table, row_index, values, err, err_sz) != 0) {
        return -1;
    }
    if (snapshot_row(table, row_index, &entry.data.row_insert.row) != 0) {
        set_err(err, err_sz, "Failed to capture inserted row");
        free_entry(&entry);
        return -1;
    }
    entry.data.row_insert.index = row_index;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, row_index, cursor_col, 0);
    return 0;
}

int ui_history_delete_row(Table *table, int row, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;
    int next_row;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_ROW_DELETE;
    if (snapshot_row(table, row, &entry.data.row_delete.row) != 0) {
        set_err(err, err_sz, "Failed to capture deleted row");
        free_entry(&entry);
        return -1;
    }
    entry.data.row_delete.index = row;
    if (tableop_delete_row(table, row, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    next_row = (row < table->row_count) ? row : table->row_count - 1;
    entry.focus_actual_row = next_row;
    entry.focus_col = cursor_col;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, next_row, cursor_col, 0);
    return 0;
}

int ui_history_move_row(Table *table, int src_row, int dst_row, int place_after, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;
    int final_index;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_ROW_MOVE;
    if (tableop_move_row(table, src_row, dst_row, place_after, err, err_sz) != 0) {
        return -1;
    }
    final_index = final_index_for_move(src_row, dst_row, place_after);
    entry.data.row_move.from_index = src_row;
    entry.data.row_move.to_index = final_index;
    entry.focus_actual_row = final_index;
    entry.focus_col = cursor_col;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, final_index, cursor_col, 0);
    return 0;
}

int ui_history_swap_rows(Table *table, int row_a, int row_b, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_ROW_SWAP;
    if (tableop_swap_rows(table, row_a, row_b, err, err_sz) != 0) {
        return -1;
    }
    entry.data.row_swap.index_a = row_a;
    entry.data.row_swap.index_b = row_b;
    entry.focus_actual_row = row_b;
    entry.focus_col = cursor_col;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, row_b, cursor_col, 0);
    return 0;
}

int ui_history_insert_column(Table *table, int col_index, const char *name, DataType type, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_INSERT;
    entry.focus_col = col_index;
    entry.focus_header = 1;
    if (tableop_insert_column_at(table, col_index, name, type, err, err_sz) != 0) {
        return -1;
    }
    if (snapshot_column(table, col_index, &entry.data.col_insert.column) != 0) {
        set_err(err, err_sz, "Failed to capture inserted column");
        free_entry(&entry);
        return -1;
    }
    entry.data.col_insert.index = col_index;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, col_index, 1);
    return 0;
}

int ui_history_delete_column(Table *table, int col, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;
    int next_col;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_DELETE;
    if (snapshot_column(table, col, &entry.data.col_delete.column) != 0) {
        set_err(err, err_sz, "Failed to capture deleted column");
        free_entry(&entry);
        return -1;
    }
    entry.data.col_delete.index = col;
    if (tableop_delete_column(table, col, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    next_col = (col < table->column_count) ? col : table->column_count - 1;
    if (next_col < 0) next_col = 0;
    entry.focus_col = next_col;
    entry.focus_header = 1;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, next_col, 1);
    return 0;
}

int ui_history_move_column(Table *table, int src_col, int dst_col, int place_after, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;
    int final_index;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_MOVE;
    if (tableop_move_column(table, src_col, dst_col, place_after, err, err_sz) != 0) {
        return -1;
    }
    final_index = final_index_for_move(src_col, dst_col, place_after);
    entry.data.col_move.from_index = src_col;
    entry.data.col_move.to_index = final_index;
    entry.focus_col = final_index;
    entry.focus_header = 1;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, final_index, 1);
    return 0;
}

int ui_history_swap_columns(Table *table, int col_a, int col_b, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_SWAP;
    if (tableop_swap_columns(table, col_a, col_b, err, err_sz) != 0) {
        return -1;
    }
    entry.data.col_swap.index_a = col_a;
    entry.data.col_swap.index_b = col_b;
    entry.focus_col = col_b;
    entry.focus_header = 1;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, col_b, 1);
    return 0;
}

int ui_history_rename_column(Table *table, int col, const char *name, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_RENAME;
    entry.data.col_rename.col = col;
    entry.data.col_rename.before_name = dup_or_empty(table->columns[col].name);
    entry.data.col_rename.after_name = dup_or_empty(name);
    if (!entry.data.col_rename.before_name || !entry.data.col_rename.after_name) {
        set_err(err, err_sz, "Out of memory");
        free_entry(&entry);
        return -1;
    }
    if (tableop_rename_column(table, col, name, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    entry.focus_col = col;
    entry.focus_header = 1;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, col, 1);
    return 0;
}

int ui_history_change_column_type(Table *table, int col, DataType type, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    memset(&entry, 0, sizeof(entry));
    entry.kind = UIH_ACTION_COL_TYPE;
    entry.data.col_type.col = col;
    if (snapshot_column(table, col, &entry.data.col_type.before) != 0) {
        set_err(err, err_sz, "Failed to capture existing column");
        free_entry(&entry);
        return -1;
    }
    if (tableop_change_column_type(table, col, type, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    if (snapshot_column(table, col, &entry.data.col_type.after) != 0) {
        set_err(err, err_sz, "Failed to capture updated column");
        free_entry(&entry);
        return -1;
    }
    entry.focus_col = col;
    entry.focus_header = 1;
    if (push_undo(&entry, err, err_sz) != 0 || autosave_after_history(table, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    fill_result(result, -1, col, 1);
    return 0;
}

static int apply_entry(Table *table, const UiHistoryEntry *entry, int undo, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    int rc = -1;
    int focus_row = entry->focus_actual_row;
    int focus_col = entry->focus_col;
    int focus_header = entry->focus_header;

    if (!table || !entry) {
        set_err(err, err_sz, "Missing history entry");
        return -1;
    }

    switch (entry->kind) {
        case UIH_ACTION_CELL_SET:
        case UIH_ACTION_CELL_CLEAR:
            rc = apply_cell_value(table,
                                  entry->data.cell.row,
                                  entry->data.cell.col,
                                  undo ? &entry->data.cell.before : &entry->data.cell.after,
                                  err,
                                  err_sz);
            focus_row = entry->data.cell.row;
            focus_col = entry->data.cell.col;
            focus_header = 0;
            break;
        case UIH_ACTION_ROW_INSERT:
            rc = undo ? tableop_delete_row(table, entry->data.row_insert.index, err, err_sz)
                      : insert_row_snapshot(table, entry->data.row_insert.index, &entry->data.row_insert.row, err, err_sz);
            focus_row = undo ? ((entry->data.row_insert.index < table->row_count) ? entry->data.row_insert.index : table->row_count - 1)
                             : entry->data.row_insert.index;
            focus_col = entry->focus_col;
            focus_header = 0;
            break;
        case UIH_ACTION_ROW_DELETE:
            rc = undo ? insert_row_snapshot(table, entry->data.row_delete.index, &entry->data.row_delete.row, err, err_sz)
                      : tableop_delete_row(table, entry->data.row_delete.index, err, err_sz);
            focus_row = undo ? entry->data.row_delete.index
                             : ((entry->data.row_delete.index < table->row_count) ? entry->data.row_delete.index : table->row_count - 1);
            focus_col = entry->focus_col;
            focus_header = 0;
            break;
        case UIH_ACTION_ROW_MOVE:
            rc = undo ? move_row_exact(table, entry->data.row_move.to_index, entry->data.row_move.from_index, err, err_sz)
                      : move_row_exact(table, entry->data.row_move.from_index, entry->data.row_move.to_index, err, err_sz);
            focus_row = undo ? entry->data.row_move.from_index : entry->data.row_move.to_index;
            focus_col = entry->focus_col;
            focus_header = 0;
            break;
        case UIH_ACTION_ROW_SWAP:
            rc = tableop_swap_rows(table, entry->data.row_swap.index_a, entry->data.row_swap.index_b, err, err_sz);
            focus_row = entry->data.row_swap.index_b;
            focus_col = entry->focus_col;
            focus_header = 0;
            break;
        case UIH_ACTION_COL_INSERT:
            rc = undo ? tableop_delete_column(table, entry->data.col_insert.index, err, err_sz)
                      : insert_column_snapshot(table, entry->data.col_insert.index, &entry->data.col_insert.column, err, err_sz);
            focus_row = -1;
            focus_col = undo ? ((entry->data.col_insert.index < table->column_count) ? entry->data.col_insert.index : table->column_count - 1)
                             : entry->data.col_insert.index;
            if (focus_col < 0) focus_col = 0;
            focus_header = 1;
            break;
        case UIH_ACTION_COL_DELETE:
            rc = undo ? insert_column_snapshot(table, entry->data.col_delete.index, &entry->data.col_delete.column, err, err_sz)
                      : tableop_delete_column(table, entry->data.col_delete.index, err, err_sz);
            focus_row = -1;
            focus_col = undo ? entry->data.col_delete.index
                             : ((entry->data.col_delete.index < table->column_count) ? entry->data.col_delete.index : table->column_count - 1);
            if (focus_col < 0) focus_col = 0;
            focus_header = 1;
            break;
        case UIH_ACTION_COL_MOVE:
            rc = undo ? move_column_exact(table, entry->data.col_move.to_index, entry->data.col_move.from_index, err, err_sz)
                      : move_column_exact(table, entry->data.col_move.from_index, entry->data.col_move.to_index, err, err_sz);
            focus_row = -1;
            focus_col = undo ? entry->data.col_move.from_index : entry->data.col_move.to_index;
            focus_header = 1;
            break;
        case UIH_ACTION_COL_SWAP:
            rc = tableop_swap_columns(table, entry->data.col_swap.index_a, entry->data.col_swap.index_b, err, err_sz);
            focus_row = -1;
            focus_col = entry->data.col_swap.index_b;
            focus_header = 1;
            break;
        case UIH_ACTION_COL_RENAME:
            rc = tableop_rename_column(table,
                                       entry->data.col_rename.col,
                                       undo ? entry->data.col_rename.before_name : entry->data.col_rename.after_name,
                                       err,
                                       err_sz);
            focus_row = -1;
            focus_col = entry->data.col_rename.col;
            focus_header = 1;
            break;
        case UIH_ACTION_COL_TYPE:
            rc = apply_column_snapshot_in_place(table,
                                               entry->data.col_type.col,
                                               undo ? &entry->data.col_type.before : &entry->data.col_type.after,
                                               err,
                                               err_sz);
            focus_row = -1;
            focus_col = entry->data.col_type.col;
            focus_header = 1;
            break;
        default:
            set_err(err, err_sz, "Unsupported history action");
            rc = -1;
            break;
    }

    if (rc != 0) return -1;
    fill_result(result, focus_row, focus_col, focus_header);
    return 0;
}

int ui_history_undo(Table *table, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    if (g_undo_count <= 0) {
        set_err(err, err_sz, "Nothing to undo.");
        return -1;
    }

    entry = g_undo[--g_undo_count];
    memset(&g_undo[g_undo_count], 0, sizeof(g_undo[g_undo_count]));
    if (apply_entry(table, &entry, 1, result, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    push_redo(&entry);
    return autosave_after_history(table, err, err_sz);
}

int ui_history_redo(Table *table, UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    UiHistoryEntry entry;

    if (g_redo_count <= 0) {
        set_err(err, err_sz, "Nothing to redo.");
        return -1;
    }

    entry = g_redo[--g_redo_count];
    memset(&g_redo[g_redo_count], 0, sizeof(g_redo[g_redo_count]));
    if (apply_entry(table, &entry, 0, result, err, err_sz) != 0) {
        free_entry(&entry);
        return -1;
    }
    if (g_undo_count >= UI_HISTORY_LIMIT) {
        discard_oldest(g_undo, &g_undo_count);
    }
    g_undo[g_undo_count++] = entry;
    return autosave_after_history(table, err, err_sz);
}

int ui_history_refresh(Table *table, const UiHistoryApplyResult *result, char *err, size_t err_sz)
{
    if (!table) {
        set_err(err, err_sz, "No table");
        return -1;
    }
    if (ui_rebuild_table_view(table, err, err_sz) != 0) {
        return -1;
    }
    if (result) {
        ui_focus_location(table, result->focus_actual_row, result->focus_col, result->focus_header);
    }
    return 0;
}
