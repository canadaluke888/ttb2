/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Layout calculations for visible columns, rows, and gutters. */

#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

int ui_compute_column_widths(Table *table, int *col_widths)
{
    int visible_row_count;
    int rows_vis_est;
    int rstart_est;
    int rend_est;

    if (!table || !col_widths || table->column_count <= 0) return -1;

    visible_row_count = ui_visible_row_count(table);
    rows_vis_est = (LINES - 5 - 3) / 2;
    if (rows_vis_est < 1) rows_vis_est = 1;

    rstart_est = row_page * rows_vis_est;
    if (rstart_est < 0) rstart_est = 0;
    if (rstart_est > visible_row_count) rstart_est = visible_row_count;

    rend_est = rstart_est + rows_vis_est;
    if (rend_est > visible_row_count) rend_est = visible_row_count;

    for (int j = 0; j < table->column_count; ++j) {
        char header_buf[128];
        int max_width;

        snprintf(header_buf, sizeof(header_buf), "%s (%s)", table->columns[j].name, type_to_string(table->columns[j].type));
        max_width = ui_text_width(header_buf) + 2;

        for (int i = rstart_est; i < rend_est; ++i) {
            int actual_row = ui_actual_row_for_visible(table, i);
            char buf[64];
            int width;

            if (actual_row < 0) continue;
            ui_format_cell_value(table, actual_row, j, buf, sizeof(buf));
            width = ui_numeric_column_uses_sign_slot(table, j)
                        ? (ui_numeric_text_width_for_grid(buf) + 2)
                        : (ui_text_width(buf) + 2);
            if (width > max_width) max_width = width;
        }

        col_widths[j] = max_width;
    }

    return 0;
}

int ui_alloc_column_widths(Table *table, int **col_widths_out)
{
    int *col_widths;

    if (!col_widths_out || !table || table->column_count <= 0) return -1;

    col_widths = malloc(sizeof(int) * table->column_count);
    if (!col_widths) return -1;
    if (ui_compute_column_widths(table, col_widths) != 0) {
        free(col_widths);
        return -1;
    }

    *col_widths_out = col_widths;
    return 0;
}

static int ui_layout_available_width(int *use_gutter, int *gutter_w)
{
    int available = COLS - 4;
    int rows_vis_est = (LINES - 5 - 3) / 2;

    if (rows_vis_est < 1) rows_vis_est = 1;

    *use_gutter = row_gutter_enabled ? 1 : 0;
    *gutter_w = 0;

    if (*use_gutter) {
        long long max_row_num = (long long)(row_page * rows_vis_est + rows_vis_est);
        long long tmpn;

        if (max_row_num < 1) max_row_num = 1;
        tmpn = max_row_num;
        *gutter_w = 1;
        while (tmpn >= 10) {
            (*gutter_w)++;
            tmpn /= 10;
        }
        *gutter_w += 2;
        available -= (*gutter_w + 1);
        if (available < 10) available = 10;
    }

    return available;
}

void ui_update_column_paging(Table *table, const int *col_widths)
{
    int available;
    int use_gutter;
    int gutter_w;
    int max_pages;
    int *page_starts;
    int pages = 0;
    int i = 0;

    if (!table || table->column_count <= 0 || !col_widths) return;

    available = ui_layout_available_width(&use_gutter, &gutter_w);
    (void)use_gutter;
    (void)gutter_w;

    max_pages = table->column_count > 0 ? table->column_count : 1;
    page_starts = malloc(max_pages * sizeof(int));
    if (!page_starts) return;

    while (i < table->column_count) {
        int wsum = 0;
        int j = i;
        int count = 0;

        page_starts[pages++] = i;
        while (j < table->column_count) {
            if (count > 0) {
                if (wsum + 1 + col_widths[j] > available) break;
                wsum += 1 + col_widths[j];
            } else {
                if (col_widths[j] > available) {
                    wsum = col_widths[j];
                    break;
                }
                wsum = col_widths[j];
            }
            count++;
            j++;
        }
        if (count <= 0) {
            count = 1;
            j = i + 1;
        }
        i = j;
    }

    if ((search_mode || reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
        cursor_col >= 0 && cursor_col < table->column_count) {
        for (int p = 0; p < pages; ++p) {
            int s = page_starts[p];
            int wsum = 0;
            int visible = 0;

            for (int j = s; j < table->column_count; ++j) {
                if (visible == 0) {
                    if (col_widths[j] > available) {
                        visible = 1;
                        break;
                    }
                    wsum = col_widths[j];
                    visible = 1;
                } else {
                    if (wsum + 1 + col_widths[j] > available) break;
                    wsum += 1 + col_widths[j];
                    visible++;
                }
            }
            if (cursor_col >= s && cursor_col < s + (visible > 0 ? visible : 1)) {
                col_page = p;
                break;
            }
        }
    }

    if (col_page < 0) col_page = 0;
    if (col_page >= pages) col_page = (pages > 0 ? pages - 1 : 0);

    col_start = (pages > 0 ? page_starts[col_page] : 0);
    cols_visible = 0;
    for (int j = col_start, wsum = 0; j < table->column_count; ++j) {
        if (cols_visible == 0) {
            if (col_widths[j] > available) {
                cols_visible = 1;
                break;
            }
            wsum = col_widths[j];
            cols_visible = 1;
        } else {
            if (wsum + 1 + col_widths[j] > available) break;
            wsum += 1 + col_widths[j];
            cols_visible++;
        }
    }
    if (cols_visible <= 0) cols_visible = 1;
    total_pages = pages > 0 ? pages : 1;

    free(page_starts);
}

void ui_update_row_paging(Table *table)
{
    int visible_row_count;
    int grid_available_lines = LINES - 5;
    int max_rows = (grid_available_lines - 3) / 2;

    if (!table) return;

    if (max_rows < 1) max_rows = 1;
    rows_visible = max_rows;

    visible_row_count = ui_visible_row_count(table);
    total_row_pages = (visible_row_count + rows_visible - 1) / rows_visible;
    if (total_row_pages <= 0) total_row_pages = 1;

    if (search_mode && cursor_row >= 0 && cursor_row < visible_row_count && rows_visible > 0) {
        row_page = cursor_row / rows_visible;
    }
    if (row_page >= total_row_pages) row_page = (total_row_pages > 0 ? total_row_pages - 1 : 0);
}

void ui_fill_grid_layout(Table *table, UiGridLayout *layout)
{
    int available;

    if (!layout) return;

    layout->start_col = col_start;
    layout->end_col = table ? col_start + cols_visible : 0;
    if (table && layout->end_col > table->column_count) layout->end_col = table->column_count;
    available = ui_layout_available_width(&layout->use_gutter, &layout->gutter_width);
    (void)available;
}
