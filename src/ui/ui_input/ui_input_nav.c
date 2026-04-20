/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Keyboard navigation helpers for moving through the grid. */

#include "ui/internal.h"

void ui_ensure_cursor_column_visible(const Table *table)
{
    if (!table || table->column_count <= 0) {
        col_page = 0;
        return;
    }

    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col >= table->column_count) cursor_col = table->column_count - 1;

    while (cols_visible > 0 && cursor_col < col_start && col_page > 0) col_page--;
    while (cols_visible > 0 && cursor_col >= col_start + cols_visible && col_page < total_pages - 1) col_page++;
}

void ui_ensure_cursor_row_visible(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0 || rows_visible <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }

    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
    row_page = cursor_row / rows_visible;
}

void ui_move_cursor_left_paged(const Table *table)
{
    if (!table || table->column_count <= 0) return;
    if (cursor_col > 0) cursor_col--;
    ui_ensure_cursor_column_visible(table);
}

void ui_move_cursor_right_paged(const Table *table)
{
    if (!table || table->column_count <= 0) return;
    if (cursor_col < table->column_count - 1) cursor_col++;
    ui_ensure_cursor_column_visible(table);
}

void ui_move_cursor_up_paged(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }
    if (cursor_row > 0) cursor_row--;
    ui_ensure_cursor_row_visible(table);
}

void ui_move_cursor_down_paged(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }
    if (cursor_row < visible_rows - 1) cursor_row++;
    ui_ensure_cursor_row_visible(table);
}

int ui_current_page_last_col(const Table *table)
{
    int cmax;

    if (!table || table->column_count <= 0) return 0;
    cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
    if (cmax >= table->column_count) cmax = table->column_count - 1;
    if (cmax < 0) cmax = 0;
    return cmax;
}

int ui_current_page_last_row(Table *table)
{
    int visible_rows = ui_visible_row_count(table);
    int rstart;
    int rmax;

    if (visible_rows <= 0) return -1;
    rstart = row_page * (rows_visible > 0 ? rows_visible : 1);
    if (rstart < 0) rstart = 0;
    rmax = rstart + (rows_visible > 0 ? rows_visible - 1 : 0);
    if (rmax >= visible_rows) rmax = visible_rows - 1;
    return rmax;
}

void ui_move_cursor_left_cross_page(const Table *table)
{
    if (!table || table->column_count <= 0) return;
    if (cursor_col > col_start) {
        cursor_col--;
        return;
    }
    if (col_page > 0) {
        col_page--;
        ui_ensure_cursor_column_visible(table);
        cursor_col = ui_current_page_last_col(table);
    }
}

void ui_move_cursor_right_cross_page(const Table *table)
{
    int cmax;

    if (!table || table->column_count <= 0) return;
    cmax = ui_current_page_last_col(table);
    if (cursor_col < cmax) {
        cursor_col++;
        return;
    }
    if (col_page < total_pages - 1) {
        col_page++;
        ui_ensure_cursor_column_visible(table);
        cursor_col = col_start;
    }
}

void ui_move_cursor_up_cross_page(Table *table)
{
    int visible_rows = ui_visible_row_count(table);
    int rmin;

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }

    rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
    if (rmin < 0) rmin = 0;

    if (cursor_row == -1) {
        if (row_page > 0) {
            row_page--;
            cursor_row = ui_current_page_last_row(table);
        }
        return;
    }

    if (cursor_row > rmin) {
        cursor_row--;
        return;
    }

    if (row_page > 0) {
        row_page--;
        cursor_row = ui_current_page_last_row(table);
    } else {
        cursor_row = -1;
    }
}

void ui_move_cursor_down_cross_page(Table *table)
{
    int visible_rows = ui_visible_row_count(table);
    int rmax;

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }

    rmax = ui_current_page_last_row(table);
    if (cursor_row < 0) {
        int rstart = row_page * (rows_visible > 0 ? rows_visible : 1);

        if (rstart >= visible_rows) rstart = visible_rows - 1;
        if (rstart < 0) rstart = 0;
        cursor_row = rstart;
        return;
    }

    if (cursor_row < rmax) {
        cursor_row++;
        return;
    }

    if (row_page < total_row_pages - 1) {
        row_page++;
        cursor_row = row_page * (rows_visible > 0 ? rows_visible : 1);
        if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
    }
}

void ui_clamp_cursor_viewport(const Table *table)
{
    int visible_rows = tableview_visible_row_count((Table *)table, &ui_table_view);

    if (search_mode) {
        if (table) {
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= table->column_count) cursor_col = (table->column_count > 0) ? (table->column_count - 1) : 0;
        }
        if (visible_rows <= 0) {
            cursor_row = -1;
        } else {
            if (cursor_row < 0) cursor_row = 0;
            if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
        }
        return;
    }

    if (ui_reorder_active()) {
        if (table) {
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= table->column_count) cursor_col = (table->column_count > 0) ? (table->column_count - 1) : 0;
        }
        if (visible_rows <= 0) {
            cursor_row = -1;
            row_page = 0;
        } else {
            if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                if (cursor_row < -1) cursor_row = -1;
            } else if (cursor_row < 0) {
                cursor_row = 0;
            }
            if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
            if (cursor_row >= 0 && rows_visible > 0) row_page = cursor_row / rows_visible;
        }
        return;
    }

    {
        int cmin = col_start;
        int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table ? table->column_count - 1 : 0);
        int rmin;
        int rmax;

        if (cmax >= (table ? table->column_count : 0)) cmax = (table ? table->column_count - 1 : 0);
        if (cursor_col < cmin) cursor_col = cmin;
        if (cursor_col > cmax) cursor_col = (cmax >= 0 ? cmax : 0);

        rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
        if (rmin < 0) rmin = 0;
        rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
        if (rmax >= visible_rows) rmax = visible_rows - 1;
        if (cursor_row >= 0) {
            if (cursor_row < rmin) cursor_row = rmin;
            if (cursor_row > rmax) cursor_row = rmax;
        }
        if (visible_rows <= 0) cursor_row = -1;
    }
}
