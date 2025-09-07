#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tablecraft.h"
#include "ui.h"
#include "db_manager.h"

void draw_table_grid(Table *t) {
    if (t->column_count == 0)
        return;

    int x = 2, y = 2;
    int *col_widths = malloc(t->column_count * sizeof(int));

    for (int j = 0; j < t->column_count; j++) {
        char header_buf[128];
        snprintf(header_buf, sizeof(header_buf), "%s (%s)",
                 t->columns[j].name, type_to_string(t->columns[j].type));
        int max = strlen(header_buf) + 2;

        for (int i = 0; i < t->row_count; i++) {
            char buf[64];
            if (!t->rows[i].values[j])
                continue;
            if (t->columns[j].type == TYPE_INT)
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_FLOAT)
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_BOOL)
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[i].values[j]) ? "true" : "false");
            else
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[i].values[j]);

            int len = strlen(buf) + 2;
            if (len > max)
                max = len;
        }
        col_widths[j] = max;
    }

    // Determine visible columns for current page to fit in COLS
    int available = COLS - x - 2; // some padding
    // Precompute page starts based on widths
    int max_pages = t->column_count > 0 ? t->column_count : 1;
    int *page_starts = malloc(max_pages * sizeof(int));
    int pages = 0;
    int i = 0;
    while (i < t->column_count) {
        page_starts[pages++] = i;
        int wsum = 0;
        int j = i;
        int count = 0;
        while (j < t->column_count) {
            if (count > 0) {
                if (wsum + 1 + col_widths[j] > available) break; // +1 for separator
                wsum += 1 + col_widths[j];
            } else {
                if (col_widths[j] > available) { wsum = col_widths[j]; break; }
                wsum = col_widths[j];
            }
            count++;
            j++;
        }
        if (count <= 0) { count = 1; j = i + 1; }
        i = j;
    }

    // If in search mode, ensure the page shows the cursor column
    if (search_mode && t->column_count > 0 && cursor_col >= 0 && cursor_col < t->column_count) {
        for (int p = 0; p < pages; ++p) {
            int s = page_starts[p];
            int wsum2 = 0, vis2 = 0;
            for (int j = s; j < t->column_count; ++j) {
                if (vis2 == 0) {
                    if (col_widths[j] > available) { vis2 = 1; break; }
                    wsum2 = col_widths[j];
                    vis2 = 1;
                } else {
                    if (wsum2 + 1 + col_widths[j] > available) break;
                    wsum2 += 1 + col_widths[j];
                    vis2++;
                }
            }
            int e = s + (vis2 > 0 ? vis2 : 1);
            if (cursor_col >= s && cursor_col < e) { col_page = p; break; }
        }
    }
    // Clamp page index and derive current start from page index
    if (col_page < 0) col_page = 0;
    if (col_page >= pages) col_page = (pages > 0 ? pages - 1 : 0);
    int start = (pages > 0 ? page_starts[col_page] : 0);
    // Compute visible count for chosen start
    int end = t->column_count;
    int wsum = 0; cols_visible = 0;
    for (int j = start; j < t->column_count; ++j) {
        if (cols_visible == 0) {
            if (col_widths[j] > available) { cols_visible = 1; break; }
            wsum = col_widths[j];
            cols_visible = 1;
        } else {
            if (wsum + 1 + col_widths[j] > available) break;
            wsum += 1 + col_widths[j];
            cols_visible++;
        }
    }
    if (cols_visible <= 0) cols_visible = 1;
    end = start + cols_visible; if (end > t->column_count) end = t->column_count;
    total_pages = pages > 0 ? pages : 1;
    col_start = start;

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┏");
    for (int j = start; j < end; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("━");
        addstr((j < end - 1) ? "┳" : "┓");
    }
    attroff(COLOR_PAIR(6));

    move(y++, x);
    attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    for (int j = start; j < end; j++) {
        const char *name = t->columns[j].name;
        const char *type = type_to_string(t->columns[j].type);

        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j)
            attron(A_REVERSE);

        attron(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);
        printw(" %s", name);
        attroff(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);

        attron(COLOR_PAIR(3));
        printw(" (%s)", type);
        attroff(COLOR_PAIR(3));

        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j)
            attroff(A_REVERSE);

        int used = strlen(name) + strlen(type) + 4;
        for (int s = used; s < col_widths[j]; s++)
            addch(' ');

        attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┡");
    for (int j = start; j < end; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("━");
        addstr((j < end - 1) ? "╇" : "┩");
    }
    attroff(COLOR_PAIR(6));

    // Calculate vertical paging capacity
    int grid_available_lines = LINES - 4; // space between title and footer
    int max_rows = (grid_available_lines - 3) / 2; // from 2*N + 3 <= available
    if (max_rows < 1) max_rows = 1;
    rows_visible = max_rows;
    total_row_pages = (t->row_count + rows_visible - 1) / rows_visible;
    // If in search mode, ensure the visible rows include the cursor row
    if (search_mode && cursor_row >= 0 && cursor_row < t->row_count && rows_visible > 0) {
        row_page = cursor_row / rows_visible;
    }
    if (row_page >= total_row_pages) row_page = (total_row_pages > 0 ? total_row_pages - 1 : 0);
    int rstart = row_page * rows_visible;
    if (rstart < 0) rstart = 0;
    int rend = rstart + rows_visible;
    if (rend > t->row_count) rend = t->row_count;

    for (int i = rstart; i < rend; i++) {
        move(y++, x);
        attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        for (int j = start; j < end; j++) {
            char buf[64] = "";
            if (t->columns[j].type == TYPE_INT && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_FLOAT && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_BOOL && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[i].values[j]) ? "true" : "false");
            else if (t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[i].values[j]);

            if ((editing_mode || search_mode) && cursor_row == i && cursor_col == j)
                attron(A_REVERSE);

            attron(COLOR_PAIR(t->columns[j].color_pair_id));
            printw(" %s", buf);
            int used = strlen(buf) + 1;
            for (int s = used; s < col_widths[j]; s++)
                addch(' ');
            attroff(COLOR_PAIR(t->columns[j].color_pair_id));

            if ((editing_mode || search_mode) && cursor_row == i && cursor_col == j)
                attroff(A_REVERSE);

            attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        }
        if (i < rend - 1) {
            move(y++, x);
            attron(COLOR_PAIR(6));
            addstr("├");
            for (int j = start; j < end; j++) {
                for (int k = 0; k < col_widths[j]; k++)
                    addstr("─");
                addstr((j < end - 1) ? "┼" : "┤");
            }
            attroff(COLOR_PAIR(6));
        }
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "└");
    for (int j = start; j < end; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("─");
        addstr((j < end - 1) ? "┴" : "┘");
    }
    attroff(COLOR_PAIR(6));

    free(page_starts);
    free(col_widths);
}

void draw_ui(Table *table) {
    clear();

    int title_x = (COLS - (int)strlen(table->name)) / 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, title_x, "%s", table->name);
    attroff(COLOR_PAIR(1) | A_BOLD);

    // Show cursor position at top-left when in edit or search mode
    if (editing_mode || search_mode) {
        int rcur = (cursor_row < 0) ? 0 : (cursor_row + 1);
        int rtot = table->row_count;
        int ccur = (table->column_count > 0) ? (cursor_col + 1) : 0;
        int ctot = table->column_count;
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(0, 2, "R %d/%d  C %d/%d", rcur, rtot, ccur, ctot);
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    // Show current DB at top right
    // Show DB status at top-right without overlapping title
    DbManager *adb = db_get_active();
    const char *full = (adb && db_is_connected(adb)) ? db_current_path(adb) : NULL;
    char shown[128];
    if (!full) {
        snprintf(shown, sizeof(shown), "No Database Connected");
    } else {
        // basename
        const char *base = strrchr(full, '/');
        if (!base) base = full; else base++;
        snprintf(shown, sizeof(shown), "DB: %s", base);
    }
    int len = (int)strlen(shown);
    int posx = COLS - len - 2; if (posx < 0) posx = 0;
    // If overlap with centered title, truncate with ellipsis
    int title_end = title_x + (int)strlen(table->name);
    if (posx <= title_end + 1) {
        // compute max width for right area
        int maxw = COLS - (title_end + 4);
        if (maxw < 8) maxw = 8; // minimal width
        if (maxw < len) {
            // truncate from left with ellipsis
            if (maxw >= 3) {
                char buf[128];
                int copy = maxw - 3;
                if (copy < 0) copy = 0;
                snprintf(buf, sizeof(buf), "...%.*s", copy, shown + (len - copy));
                strncpy(shown, buf, sizeof(shown)-1);
                shown[sizeof(shown)-1] = '\0';
                len = (int)strlen(shown);
            }
        }
        posx = COLS - len - 2; if (posx < 0) posx = 0;
    }
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(0, posx, "%s", shown);
    attroff(COLOR_PAIR(3) | A_BOLD);

    draw_table_grid(table);

    // Footer: general hints vs paging hints colored differently
    int fy = LINES - 2;
    move(fy, 2);
    attron(COLOR_PAIR(5));
    if (search_mode) {
        printw("[←][→][↑][↓] Prev/Next Match   [Esc] Exit Search");
        attroff(COLOR_PAIR(5));
        // Show match index/total in paging color for grounding
        attron(COLOR_PAIR(4));
        extern int search_hit_index; extern int search_hit_count;
        printw("  |  Matches %d/%d", (search_hit_count > 0 ? (search_hit_index + 1) : 0), search_hit_count);
        attroff(COLOR_PAIR(4));
        // During search mode, we still want the table to scroll to the match; hints are simplified
    } else if (!editing_mode) {
        printw("[C] Add Column  [R] Add Row  [F] Search  [E] Edit Mode  [M] Menu  [Q] Quit");
        attroff(COLOR_PAIR(5));
        // Paging hints in a distinct color
        if (total_pages > 1 || total_row_pages > 1) {
            attron(COLOR_PAIR(4));
            if (total_pages > 1) {
                printw("  |  Cols Pg %d/%d  [←][→] Columns", col_page + 1, total_pages);
            }
            if (total_row_pages > 1) {
                printw("  |  Rows Pg %d/%d  [↑][↓] Rows", row_page + 1, total_row_pages);
            }
            attroff(COLOR_PAIR(4));
        }
    } else {
        printw("[←][→][↑][↓] Navigate    [Enter] Edit Cell    [Esc] Exit Edit Mode");
        attroff(COLOR_PAIR(5));
        // Also show paging context in edit mode if applicable
        if (total_pages > 1 || total_row_pages > 1) {
            attron(COLOR_PAIR(4));
            if (total_pages > 1) {
                printw("  |  Cols Pg %d/%d", col_page + 1, total_pages);
            }
            if (total_row_pages > 1) {
                printw("  |  Rows Pg %d/%d", row_page + 1, total_row_pages);
            }
            attroff(COLOR_PAIR(4));
        }
    }

    wnoutrefresh(stdscr);
}
