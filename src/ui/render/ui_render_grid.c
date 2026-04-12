#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

void draw_table_grid(Table *table)
{
    UiGridLayout layout;
    int visible_row_count = ui_visible_row_count(table);
    int *col_widths;
    int x = 2;
    int y = 2;

    if (!table || table->column_count == 0) {
        total_pages = 1;
        cols_visible = 0;
        col_page = 0;
        col_start = 0;
        total_row_pages = 1;
        rows_visible = 0;
        row_page = 0;
        return;
    }

    if (ui_alloc_column_widths(table, &col_widths) != 0) return;

    ui_update_column_paging(table, col_widths);
    ui_update_row_paging(table);
    ui_fill_grid_layout(table, &layout);

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┏");
    if (layout.use_gutter) {
        ui_add_repeat("━", layout.gutter_width);
        addstr((layout.start_col < layout.end_col) ? "┳" : "┓");
    }
    for (int j = layout.start_col; j < layout.end_col; ++j) {
        ui_add_repeat("━", col_widths[j]);
        addstr((j < layout.end_col - 1) ? "┳" : "┓");
    }
    attroff(COLOR_PAIR(6));

    move(y++, x);
    attron(COLOR_PAIR(6));
    addstr("┃");
    attroff(COLOR_PAIR(6));
    if (layout.use_gutter) {
        int pad = layout.gutter_width - 1;
        int lp = pad / 2;
        int rp = pad - lp;

        for (int i = 0; i < lp; ++i) addch(' ');
        attron(COLOR_PAIR(3) | A_BOLD);
        addch('#');
        attroff(COLOR_PAIR(3) | A_BOLD);
        for (int i = 0; i < rp; ++i) addch(' ');
        attron(COLOR_PAIR(6));
        addstr("┃");
        attroff(COLOR_PAIR(6));
    }
    for (int j = layout.start_col; j < layout.end_col; ++j) {
        const char *name = table->columns[j].name;
        const char *type = type_to_string(table->columns[j].type);
        int remaining;
        int used = 0;
        int highlight_source = editing_mode &&
            (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
            reorder_source_col == j;
        int highlight_dest = editing_mode &&
            (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
            cursor_col == j;

        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j) attron(A_REVERSE);
        if (editing_mode && del_col_mode && cursor_col == j) attron(A_REVERSE);
        if (highlight_dest) attron(A_REVERSE);
        if (highlight_source) attron(A_REVERSE);

        attron(COLOR_PAIR(table->columns[j].color_pair_id) | A_BOLD);
        addch(' ');
        used = 1;
        used += ui_text_addstr_width(stdscr, name, col_widths[j] - used);
        attroff(COLOR_PAIR(table->columns[j].color_pair_id) | A_BOLD);

        attron(COLOR_PAIR(3));
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, " (", remaining);
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, type, remaining);
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, ")", remaining);
        attroff(COLOR_PAIR(3));

        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j) attroff(A_REVERSE);
        if (editing_mode && del_col_mode && cursor_col == j) attroff(A_REVERSE);
        if (highlight_dest) attroff(A_REVERSE);
        if (highlight_source) attroff(A_REVERSE);

        for (int s = used; s < col_widths[j]; ++s) addch(' ');

        attron(COLOR_PAIR(6));
        addstr("┃");
        attroff(COLOR_PAIR(6));
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┡");
    if (layout.use_gutter) {
        ui_add_repeat("━", layout.gutter_width);
        addstr((layout.start_col < layout.end_col) ? "╇" : "┩");
    }
    for (int j = layout.start_col; j < layout.end_col; ++j) {
        ui_add_repeat("━", col_widths[j]);
        addstr((j < layout.end_col - 1) ? "╇" : "┩");
    }
    attroff(COLOR_PAIR(6));

    for (int i = row_page * rows_visible, row_end = row_page * rows_visible + rows_visible;
         i < row_end && i < visible_row_count; ++i) {
        int actual_row = ui_actual_row_for_visible(table, i);

        move(y++, x);
        attron(COLOR_PAIR(6));
        addstr("│");
        attroff(COLOR_PAIR(6));
        if (layout.use_gutter) {
            long long rn = seek_mode_active() ? (layout.row_number_base + (i - row_page * rows_visible)) : (long long)(i + 1);
            char buf[32];
            int numlen;
            int pad;
            int lp;
            int rp;

            snprintf(buf, sizeof(buf), "%lld", rn);
            numlen = (int)strlen(buf);
            if (numlen > layout.gutter_width) numlen = layout.gutter_width;
            pad = layout.gutter_width - numlen;
            lp = pad / 2;
            rp = pad - lp;
            for (int p = 0; p < lp; ++p) addch(' ');
            attron(COLOR_PAIR(4));
            addnstr(buf, numlen);
            attroff(COLOR_PAIR(4));
            for (int p = 0; p < rp; ++p) addch(' ');
            attron(COLOR_PAIR(6));
            addstr("│");
            attroff(COLOR_PAIR(6));
        }

        for (int j = layout.start_col; j < layout.end_col; ++j) {
            char buf[64] = "";
            int highlight_cell = 0;
            int highlight_source = 0;

            if (actual_row >= 0) ui_format_cell_value(table, actual_row, j, buf, sizeof(buf));

            if ((editing_mode || search_mode) && cursor_row == i && cursor_col == j) highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) && i == cursor_row)
                highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) && j == cursor_col)
                highlight_cell = 1;
            if (editing_mode && del_row_mode && i == cursor_row) highlight_cell = 1;
            if (editing_mode && del_col_mode && j == cursor_col) highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) && i == reorder_source_row)
                highlight_source = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) && j == reorder_source_col)
                highlight_source = 1;
            if (highlight_source) attron(A_REVERSE);
            if (highlight_cell) attron(A_REVERSE);

            if (search_mode && cursor_row == i && cursor_col == j && !ui_numeric_column_uses_sign_slot(table, j)) {
                ui_draw_highlighted_cell_text(buf, col_widths[j], table->columns[j].color_pair_id, search_sel_start, search_sel_len);
            } else {
                ui_draw_cell_value(table, j, buf, col_widths[j], table->columns[j].color_pair_id);
            }

            if (highlight_cell) attroff(A_REVERSE);
            if (highlight_source) attroff(A_REVERSE);

            attron(COLOR_PAIR(6));
            addstr("│");
            attroff(COLOR_PAIR(6));
        }

        if (i < visible_row_count - 1 && i < row_page * rows_visible + rows_visible - 1) {
            move(y++, x);
            attron(COLOR_PAIR(6));
            addstr("├");
            if (layout.use_gutter) {
                ui_add_repeat("─", layout.gutter_width);
                addstr((layout.start_col < layout.end_col) ? "┼" : "┤");
            }
            for (int j = layout.start_col; j < layout.end_col; ++j) {
                ui_add_repeat("─", col_widths[j]);
                addstr((j < layout.end_col - 1) ? "┼" : "┤");
            }
            attroff(COLOR_PAIR(6));
        }
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "└");
    if (layout.use_gutter) {
        ui_add_repeat("─", layout.gutter_width);
        addstr((layout.start_col < layout.end_col) ? "┴" : "┘");
    }
    for (int j = layout.start_col; j < layout.end_col; ++j) {
        ui_add_repeat("─", col_widths[j]);
        addstr((j < layout.end_col - 1) ? "┴" : "┘");
    }
    attroff(COLOR_PAIR(6));

    free(col_widths);
}
