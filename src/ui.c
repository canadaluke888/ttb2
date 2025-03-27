#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include "../include/tablecraft.h"

#define MAX_INPUT 128

void draw_ui(Table *table);
void draw_table_grid(Table *t);
void prompt_add_column(Table *table);
void prompt_add_row(Table *table);

void start_ui_loop(Table *table) {
    int ch;

    while (1) {
        draw_ui(table);
        ch = getch();

        if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == 'c' || ch == 'C') {
            prompt_add_column(table);
        } else if (ch == 'r' || ch == 'R') {
            if (table->column_count == 0) {
                mvprintw(LINES - 4, 2, "You must add at least one column before adding rows.");
                getch();
            } else {
                prompt_add_row(table);
            }
        }
    }
}

void draw_ui(Table *table) {
    clear();
    int title_x = (COLS - strlen(table->name)) / 2;
    mvprintw(0, title_x, "%s", table->name);
    draw_table_grid(table);
    mvprintw(LINES - 2, 2, "[C] Add Column    [R] Add Row    [Q] Quit");
    refresh();
}

void prompt_add_column(Table *table) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    char type[MAX_INPUT];

    mvprintw(LINES - 5, 2, "Enter column name: ");
    getnstr(name, MAX_INPUT - 1);

    mvprintw(LINES - 4, 2, "Enter column type (int, float, str, bool): ");
    getnstr(type, MAX_INPUT - 1);

    if (strlen(name) > 0 && strlen(type) > 0) {
        DataType dtype = parse_type_from_string(type);
        add_column(table, name, dtype);
    }

    noecho();
    curs_set(0);
}

void prompt_add_row(Table *table) {
    echo();
    curs_set(1);

    char **input_strings = malloc(table->column_count * sizeof(char *));
    for (int i = 0; i < table->column_count; i++) {
        input_strings[i] = malloc(MAX_INPUT);
        char label[128];
        snprintf(label, sizeof(label), "Enter value for %s (%s): ",
                 table->columns[i].name,
                 type_to_string(table->columns[i].type));
        mvprintw(LINES - 5 + i, 2, "%s", label);
        getnstr(input_strings[i], MAX_INPUT - 1);
    }

    add_row(table, (const char **)input_strings);

    for (int i = 0; i < table->column_count; i++) {
        free(input_strings[i]);
    }
    free(input_strings);

    noecho();
    curs_set(0);
}

void draw_table_grid(Table *t) {
    if (t->column_count == 0) return;

    int x = 2, y = 2;
    int *col_widths = malloc(t->column_count * sizeof(int));

    for (int j = 0; j < t->column_count; j++) {
        // ✅ NEW: use full header label for width
        char header_buf[128];
        snprintf(header_buf, sizeof(header_buf), "%s (%s)",
                 t->columns[j].name, type_to_string(t->columns[j].type));
        int max = strlen(header_buf) + 2;

        for (int i = 0; i < t->row_count; i++) {
            char buf[64];
            if (!t->rows[i].values[j]) continue;
            if (t->columns[j].type == TYPE_INT)
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_FLOAT)
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_BOOL)
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[i].values[j]) ? "true" : "false");
            else
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[i].values[j]);

            int len = strlen(buf) + 2;
            if (len > max) max = len;
        }

        col_widths[j] = max;
    }

    // Top border
    mvprintw(y++, x, "┏");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("━");
        addstr((j < t->column_count - 1) ? "┳" : "┓");
    }

    // Header row
    move(y++, x);
    addstr("┃");
    for (int j = 0; j < t->column_count; j++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (%s)", t->columns[j].name, type_to_string(t->columns[j].type));
        printw(" %s", buf);
        int used = strlen(buf) + 1;
        for (int s = used; s < col_widths[j]; s++) addch(' ');
        addstr("┃");
    }

    // Header separator
    mvprintw(y++, x, "┡");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("━");
        addstr((j < t->column_count - 1) ? "╇" : "┩");
    }

    // Body rows
    for (int i = 0; i < t->row_count; i++) {
        move(y++, x);
        addstr("│");
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

            printw(" %s", buf);
            int used = strlen(buf) + 1;
            for (int s = used; s < col_widths[j]; s++) addch(' ');
            addstr("│");
        }

        if (i < t->row_count - 1) {
            move(y++, x);
            addstr("├");
            for (int j = 0; j < t->column_count; j++) {
                for (int k = 0; k < col_widths[j]; k++) addstr("─");
                addstr((j < t->column_count - 1) ? "┼" : "┤");
            }
        }
    }

    // Bottom border
    mvprintw(y++, x, "└");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("─");
        addstr((j < t->column_count - 1) ? "┴" : "┘");
    }

    free(col_widths);
}
