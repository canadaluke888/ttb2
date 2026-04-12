/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Filtered and sorted table view state for the visible grid. */

#ifndef TABLE_VIEW_H
#define TABLE_VIEW_H

#include <stddef.h>
#include "data/table.h"

/* Supported comparison operators for the active row filter. */
typedef enum {
    FILTER_CONTAINS,
    FILTER_EQUALS,
    FILTER_GT,
    FILTER_LT,
    FILTER_GTE,
    FILTER_LTE
} FilterOp;

/* Describes a single column filter applied to the view. */
typedef struct {
    int col;
    FilterOp op;
    char value[128];
} FilterRule;

/* Tracks filtered row order and sort state for the visible table. */
typedef struct {
    int *row_map;
    int row_map_count;

    int filter_active;
    int sort_active;

    int sort_col;
    int sort_desc;
    FilterRule filter_rule;
} TableView;

/* Table-view lifecycle helpers. */
void tableview_init(TableView *view);
void tableview_free(TableView *view);

/* Apply or clear sort and filter state. */
int tableview_sort(Table *table, TableView *view, int col, int descending, char *err, size_t err_sz);
/* Clear any active sort while leaving filter state unchanged. */
void tableview_clear_sort(TableView *view);

/* Apply a single filter rule to the visible table view. */
int tableview_apply_filter(Table *table, TableView *view, const FilterRule *rule, char *err, size_t err_sz);
/* Clear any active row filter while leaving sort state unchanged. */
void tableview_clear_filter(TableView *view);

/* Rebuild mappings and describe the active view state. */
int tableview_rebuild(Table *table, TableView *view, char *err, size_t err_sz);
/* Translate a visible row index back to the underlying table row index. */
int tableview_row_to_actual(const Table *table, const TableView *view, int visible_row);
/* Return the row count after any active filter has been applied. */
int tableview_visible_row_count(const Table *table, const TableView *view);
/* Return a short label for the given filter operator. */
const char *tableview_filter_op_label(FilterOp op);
/* Format the active sort/filter state into a caller-provided status buffer. */
int tableview_describe(const Table *table, const TableView *view, char *buf, size_t buf_sz);

#endif /* TABLE_VIEW_H */
