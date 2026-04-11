#ifndef TABLE_VIEW_H
#define TABLE_VIEW_H

#include <stddef.h>
#include "table.h"

typedef enum {
    FILTER_CONTAINS,
    FILTER_EQUALS,
    FILTER_GT,
    FILTER_LT,
    FILTER_GTE,
    FILTER_LTE
} FilterOp;

typedef struct {
    int col;
    FilterOp op;
    char value[128];
} FilterRule;

typedef struct {
    int *row_map;
    int row_map_count;

    int filter_active;
    int sort_active;

    int sort_col;
    int sort_desc;
    FilterRule filter_rule;
} TableView;

void tableview_init(TableView *view);
void tableview_free(TableView *view);

int tableview_sort(Table *table, TableView *view, int col, int descending, char *err, size_t err_sz);
void tableview_clear_sort(TableView *view);

int tableview_apply_filter(Table *table, TableView *view, const FilterRule *rule, char *err, size_t err_sz);
void tableview_clear_filter(TableView *view);

int tableview_rebuild(Table *table, TableView *view, char *err, size_t err_sz);
int tableview_row_to_actual(const Table *table, const TableView *view, int visible_row);
int tableview_visible_row_count(const Table *table, const TableView *view);
const char *tableview_filter_op_label(FilterOp op);
int tableview_describe(const Table *table, const TableView *view, char *buf, size_t buf_sz);

#endif /* TABLE_VIEW_H */
