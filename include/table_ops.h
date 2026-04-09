#ifndef TABLE_OPS_H
#define TABLE_OPS_H

#include <stddef.h>
#include "tablecraft.h"

int tableop_set_cell(Table *table, int row, int col, const char *input, char *err, size_t err_sz);
int tableop_clear_cell(Table *table, int row, int col, char *err, size_t err_sz);

int tableop_delete_row(Table *table, int row, char *err, size_t err_sz);
int tableop_delete_column(Table *table, int col, char *err, size_t err_sz);

int tableop_insert_row(Table *table, const char **values, char *err, size_t err_sz);
int tableop_insert_column(Table *table, const char *name, DataType type, char *err, size_t err_sz);

int tableop_rename_column(Table *table, int col, const char *name, char *err, size_t err_sz);
int tableop_change_column_type(Table *table, int col, DataType type, char *err, size_t err_sz);

#endif /* TABLE_OPS_H */
