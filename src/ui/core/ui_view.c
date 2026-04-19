/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Viewport, paging, and visible-row coordination for the grid. */

#include "ui/internal.h"
#include "ui/ui_history.h"

int ui_table_view_is_active(void)
{
    return ui_table_view.filter_active || ui_table_view.sort_active;
}

int ui_visible_row_count(Table *table)
{
    return tableview_visible_row_count(table, &ui_table_view);
}

int ui_actual_row_for_visible(Table *table, int visible_row)
{
    return tableview_row_to_actual(table, &ui_table_view, visible_row);
}

int ui_rebuild_table_view(Table *table, char *err, size_t err_sz)
{
    int rc = tableview_rebuild(table, &ui_table_view, err, err_sz);
    int visible_rows = ui_visible_row_count(table);

    if (rc != 0) return rc;
    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
    } else {
        if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
        if (cursor_row >= 0 && rows_visible > 0) row_page = cursor_row / rows_visible;
    }
    return 0;
}

void ui_reset_table_view(Table *table)
{
    (void)table;
    ui_search_exit();
    ui_search_service_reset();
    ui_clear_reorder_mode();
    ui_history_reset();
    footer_page = 0;
    tableview_free(&ui_table_view);
    cursor_row = -1;
    cursor_col = 0;
    col_page = 0;
    row_page = 0;
}

void ui_focus_location(Table *table, int actual_row, int col, int prefer_header)
{
    int visible_row;
    int visible_rows;

    if (!table) return;

    if (table->column_count <= 0) {
        cursor_col = 0;
        cursor_row = -1;
        col_page = 0;
        row_page = 0;
        return;
    }

    if (col < 0) col = 0;
    if (col >= table->column_count) col = table->column_count - 1;
    cursor_col = col;

    if (prefer_header) {
        cursor_row = -1;
        ui_ensure_cursor_column_visible(table);
        return;
    }

    visible_rows = ui_visible_row_count(table);
    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        ui_ensure_cursor_column_visible(table);
        return;
    }

    if (!ui_table_view_is_active()) {
        visible_row = actual_row;
    } else {
        visible_row = -1;
        for (int i = 0; i < visible_rows; ++i) {
            if (ui_actual_row_for_visible(table, i) == actual_row) {
                visible_row = i;
                break;
            }
        }
    }

    if (visible_row < 0) visible_row = 0;
    if (visible_row >= visible_rows) visible_row = visible_rows - 1;

    cursor_row = visible_row;
    ui_ensure_cursor_row_visible(table);
    ui_ensure_cursor_column_visible(table);
}
