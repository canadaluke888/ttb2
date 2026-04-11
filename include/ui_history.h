#ifndef UI_HISTORY_H
#define UI_HISTORY_H

#include <stddef.h>
#include "table.h"

typedef struct {
    int ok;
    int focus_actual_row;
    int focus_col;
    int focus_header;
} UiHistoryApplyResult;

void ui_history_reset(void);
int ui_history_can_undo(void);
int ui_history_can_redo(void);

int ui_history_set_cell(Table *table, int row, int col, const char *input, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_clear_cell(Table *table, int row, int col, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_insert_row(Table *table, int row_index, const char **values, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_delete_row(Table *table, int row, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_move_row(Table *table, int src_row, int dst_row, int place_after, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_swap_rows(Table *table, int row_a, int row_b, UiHistoryApplyResult *result, char *err, size_t err_sz);

int ui_history_insert_column(Table *table, int col_index, const char *name, DataType type, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_delete_column(Table *table, int col, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_move_column(Table *table, int src_col, int dst_col, int place_after, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_swap_columns(Table *table, int col_a, int col_b, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_rename_column(Table *table, int col, const char *name, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_change_column_type(Table *table, int col, DataType type, UiHistoryApplyResult *result, char *err, size_t err_sz);

int ui_history_undo(Table *table, UiHistoryApplyResult *result, char *err, size_t err_sz);
int ui_history_redo(Table *table, UiHistoryApplyResult *result, char *err, size_t err_sz);

int ui_history_refresh(Table *table, const UiHistoryApplyResult *result, char *err, size_t err_sz);

#endif
