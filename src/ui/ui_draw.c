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
    int start = col_page;
    if (start < 0) start = 0;
    int end = t->column_count;
    int wsum = 0;
    cols_visible = 0;
    for (int j = start; j < t->column_count; ++j) {
        if (wsum + col_widths[j] + 1 > available) break;
        wsum += col_widths[j] + 1; // include separator cell
        cols_visible++;
    }
    if (cols_visible == 0) { cols_visible = 1; }
    total_pages = (t->column_count + cols_visible - 1) / cols_visible;
    if (col_page >= total_pages) col_page = (total_pages > 0 ? total_pages - 1 : 0);
    start = col_page * cols_visible;
    if (start >= t->column_count) start = 0;
    end = start + cols_visible;
    if (end > t->column_count) end = t->column_count;

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

        if (editing_mode && cursor_row == -1 && cursor_col == j)
            attron(A_REVERSE);

        attron(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);
        printw(" %s", name);
        attroff(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);

        attron(COLOR_PAIR(3));
        printw(" (%s)", type);
        attroff(COLOR_PAIR(3));

        if (editing_mode && cursor_row == -1 && cursor_col == j)
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

    for (int i = 0; i < t->row_count; i++) {
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

            if (editing_mode && cursor_row == i && cursor_col == j)
                attron(A_REVERSE);

            attron(COLOR_PAIR(t->columns[j].color_pair_id));
            printw(" %s", buf);
            int used = strlen(buf) + 1;
            for (int s = used; s < col_widths[j]; s++)
                addch(' ');
            attroff(COLOR_PAIR(t->columns[j].color_pair_id));

            if (editing_mode && cursor_row == i && cursor_col == j)
                attroff(A_REVERSE);

            attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        }
        if (i < t->row_count - 1) {
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

    free(col_widths);
}

void draw_ui(Table *table) {
    clear();

    int title_x = (COLS - (int)strlen(table->name)) / 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, title_x, "%s", table->name);
    attroff(COLOR_PAIR(1) | A_BOLD);

    // Show current DB at top right
    // Show DB status at top-right without overlapping title
    const char *db_label = NULL;
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

    attron(COLOR_PAIR(5));
    if (!editing_mode) {
        if (total_pages > 1) {
            mvprintw(LINES - 2, 2, "[C] Add Column  [R] Add Row  [E] Edit Mode  [M] Menu  [Q] Quit  |  Pg %d/%d  [←][→] Columns", col_page + 1, total_pages);
        } else {
            mvprintw(LINES - 2, 2, "[C] Add Column  [R] Add Row  [E] Edit Mode  [M] Menu  [Q] Quit");
        }
    } else {
        mvprintw(LINES - 2, 2, "[←][→][↑][↓] Navigate    [Enter] Edit Cell    [Esc] Exit Edit Mode");
    }
    attroff(COLOR_PAIR(5));

    wnoutrefresh(stdscr);
}
