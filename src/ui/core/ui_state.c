/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared UI state storage and view-management helpers. */

#include "ui/internal.h"

int editing_mode = 0;
int del_row_mode = 0;
int del_col_mode = 0;
int cursor_row = -1;
int cursor_col = 0;
int search_mode = 0;
int search_hit_count = 0;
int search_hit_index = 0;
int col_page = 0;
int cols_visible = 0;
int total_pages = 1;
int col_start = 0;
int row_page = 0;
int rows_visible = 0;
int total_row_pages = 1;
int row_gutter_enabled = 1;
int footer_page = 0;
UiReorderMode reorder_mode = UI_REORDER_NONE;
int reorder_source_row = -1;
int reorder_source_col = -1;
TableView ui_table_view;
char search_query[128];
int search_sel_start = -1;
int search_sel_len = 0;

static int pending_grid_edit = 0;

int ui_reorder_active(void)
{
    return reorder_mode != UI_REORDER_NONE;
}

void ui_clear_reorder_mode(void)
{
    reorder_mode = UI_REORDER_NONE;
    reorder_source_row = -1;
    reorder_source_col = -1;
}

void ui_advance_footer_page(void)
{
    footer_page = (footer_page + 1) % 2;
}

void ui_request_pending_grid_edit(void)
{
    pending_grid_edit = 1;
}

int ui_take_pending_grid_edit(void)
{
    int requested = pending_grid_edit;

    pending_grid_edit = 0;
    return requested;
}

void ui_enter_edit_mode(Table *table)
{
    int start_row;
    int visible_rows;

    if (!table) return;

    editing_mode = 1;
    footer_page = 0;
    ui_clear_reorder_mode();

    start_row = row_page * (rows_visible > 0 ? rows_visible : 1);
    if (start_row < 0) start_row = 0;

    visible_rows = ui_visible_row_count(table);
    cursor_row = (start_row < visible_rows) ? start_row : (visible_rows > 0 ? visible_rows - 1 : -1);
    cursor_col = col_start;
}

void ui_set_row_gutter_enabled(int enabled)
{
    row_gutter_enabled = enabled ? 1 : 0;
}

int ui_row_gutter_enabled(void)
{
    return row_gutter_enabled;
}
