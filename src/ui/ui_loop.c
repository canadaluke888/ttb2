#include <ncurses.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"

// Define global UI state variables
int editing_mode = 0;
int cursor_row = -1;
int cursor_col = 0;
int col_page = 0;
int cols_visible = 0;
int total_pages = 1;
int col_start = 0;
int row_page = 0;
int rows_visible = 0;
int total_row_pages = 1;

void start_ui_loop(Table *table) {
    keypad(stdscr, TRUE);  // Enable arrow keys
    int ch;

    while (1) {
        draw_ui(table);
        pm_update();
        ch = getch();

        if (ch == KEY_RESIZE) {
            pm_on_resize();
            continue;
        }

        if (!editing_mode) {
            if (ch == 'q' || ch == 'Q')
                break;
            else if (ch == 'c' || ch == 'C')
                prompt_add_column(table);
            else if (ch == 'r' || ch == 'R') {
                if (table->column_count == 0)
                    show_error_message("You must add at least one column before adding rows.");
                else
                    prompt_add_row(table);
            }
            else if (ch == 'e' || ch == 'E') {
                editing_mode = 1;
                cursor_row = -1;  // Set focus to header
                cursor_col = 0;
            }
            else if (ch == 'm' || ch == 'M') {
                show_table_menu(table);
            } else if (ch == KEY_LEFT) {
                if (col_page > 0) col_page--; // page index
            } else if (ch == KEY_RIGHT) {
                if (col_page < total_pages - 1) col_page++; // page index
            } else if (ch == KEY_UP) {
                if (row_page > 0) row_page--;
            } else if (ch == KEY_DOWN) {
                if (row_page < total_row_pages - 1) row_page++;
            }
        } else {
            switch (ch) {
                case KEY_LEFT:
                    // Do not page during edit; restrict to visible range
                    if (cursor_col > col_start) cursor_col--;
                    break;
                case KEY_RIGHT:
                    if (cursor_col < table->column_count - 1 &&
                        (cols_visible <= 0 || cursor_col < col_start + cols_visible - 1)) {
                        cursor_col++;
                    }
                    break;
                case KEY_UP:
                    // Header is always allowed; restrict data rows to visible page
                    if (cursor_row == -1) {
                        // stay on header
                    } else if (cursor_row > row_page * rows_visible) {
                        cursor_row--;
                    } else if (cursor_row > -1) {
                        cursor_row--; // move to header
                    }
                    break;
                case KEY_DOWN:
                    if (cursor_row < table->row_count - 1) {
                        int vis_end = row_page * rows_visible + rows_visible - 1;
                        if (rows_visible <= 0 || cursor_row < vis_end) {
                            cursor_row++;
                        }
                    }
                    break;
                case 27: // ESC key
                    editing_mode = 0;
                    break;
                case '\n': // Enter key
                    if (cursor_row == -1)
                        edit_header_cell(table, cursor_col);
                    else
                        edit_body_cell(table, cursor_row, cursor_col);
                    break;
            }
        }
    }
}
