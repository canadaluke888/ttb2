#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tablecraft.h"
#include "ui.h"

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

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┏");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("━");
        addstr((j < t->column_count - 1) ? "┳" : "┓");
    }
    attroff(COLOR_PAIR(6));

    move(y++, x);
    attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    for (int j = 0; j < t->column_count; j++) {
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
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("━");
        addstr((j < t->column_count - 1) ? "╇" : "┩");
    }
    attroff(COLOR_PAIR(6));

    for (int i = 0; i < t->row_count; i++) {
        move(y++, x);
        attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        for (int j = 0; j < t->column_count; j++) {
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
            for (int j = 0; j < t->column_count; j++) {
                for (int k = 0; k < col_widths[j]; k++)
                    addstr("─");
                addstr((j < t->column_count - 1) ? "┼" : "┤");
            }
            attroff(COLOR_PAIR(6));
        }
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "└");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++)
            addstr("─");
        addstr((j < t->column_count - 1) ? "┴" : "┘");
    }
    attroff(COLOR_PAIR(6));

    free(col_widths);
}

void draw_ui(Table *table) {
    clear();

    int title_x = (COLS - strlen(table->name)) / 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, title_x, "%s", table->name);
    attroff(COLOR_PAIR(1) | A_BOLD);

    draw_table_grid(table);

    attron(COLOR_PAIR(5));
    if (!editing_mode) {
        mvprintw(LINES - 2, 2, "[C] Add Column    [R] Add Row    [E] Edit Mode    [Q] Quit");
    } else {
        mvprintw(LINES - 2, 2, "[←][→][↑][↓] Navigate    [Enter] Edit Cell    [Esc] Exit Edit Mode");
    }
    attroff(COLOR_PAIR(5));

    refresh();
}
