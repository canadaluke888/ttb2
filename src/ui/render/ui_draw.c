/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared drawing primitives used by the UI renderers. */

#include <ncurses.h>
#include <stdio.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

void ui_draw_table_title(const Table *table)
{
    int title_x = (COLS - ui_text_width(table->name)) / 2;

    if (title_x < 0) title_x = 0;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvaddnstr(0, title_x, table->name, (int)ui_text_bytes_for_width(table->name, COLS - title_x));
    attroff(COLOR_PAIR(1) | A_BOLD);

    if (editing_mode || search_mode) {
        int rcur = (cursor_row < 0) ? 0 : (cursor_row + 1);
        int rtot = ui_visible_row_count((Table *)table);
        int ccur = (table->column_count > 0) ? (cursor_col + 1) : 0;
        int ctot = table->column_count;

        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(0, 2, "R %d/%d  C %d/%d", rcur, rtot, ccur, ctot);
        attroff(COLOR_PAIR(4) | A_BOLD);
    }
}

void ui_draw_view_status(Table *table)
{
    char view_buf[512];

    if (!ui_table_view_is_active()) return;

    attron(COLOR_PAIR(4));
    if (tableview_describe(table, &ui_table_view, view_buf, sizeof(view_buf)) != 0) {
        snprintf(view_buf, sizeof(view_buf), "View: %d/%d rows", ui_visible_row_count(table), table->row_count);
    }
    mvaddnstr(1, 2, view_buf, (int)ui_text_bytes_for_width(view_buf, COLS - 4));
    attroff(COLOR_PAIR(4));
}

void draw_ui(Table *table)
{
    erase();
    ui_draw_table_title(table);
    draw_table_grid(table);
    ui_draw_view_status(table);
    ui_draw_footer_box();
    ui_draw_footer(table);
    wnoutrefresh(stdscr);
}
