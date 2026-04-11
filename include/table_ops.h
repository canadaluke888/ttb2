#ifndef TABLE_OPS_H
#define TABLE_OPS_H

#include <stddef.h>
#include "table.h"

int tableop_set_cell(Table *table, int row, int col, const char *input, char *err, size_t err_sz);
int tableop_clear_cell(Table *table, int row, int col, char *err, size_t err_sz);

int tableop_delete_row(Table *table, int row, char *err, size_t err_sz);
int tableop_delete_column(Table *table, int col, char *err, size_t err_sz);
int tableop_move_row(Table *table, int src_row, int dst_row, int place_after, char *err, size_t err_sz);
int tableop_move_column(Table *table, int src_col, int dst_col, int place_after, char *err, size_t err_sz);
int tableop_swap_rows(Table *table, int row_a, int row_b, char *err, size_t err_sz);
int tableop_swap_columns(Table *table, int col_a, int col_b, char *err, size_t err_sz);

int tableop_insert_row_at(Table *table, int row_index, const char **values, char *err, size_t err_sz);
int tableop_insert_column_at(Table *table, int col_index, const char *name, DataType type, char *err, size_t err_sz);
int tableop_insert_row(Table *table, const char **values, char *err, size_t err_sz);
int tableop_insert_column(Table *table, const char *name, DataType type, char *err, size_t err_sz);

int tableop_rename_column(Table *table, int col, const char *name, char *err, size_t err_sz);
int tableop_change_column_type(Table *table, int col, DataType type, char *err, size_t err_sz);

#endif /* TABLE_OPS_H */
