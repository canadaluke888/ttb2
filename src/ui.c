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
    char type_str[MAX_INPUT];

    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    // ===============================
    // Step 1: Prompt for Column Name
    // ===============================
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    mvprintw(input_box_y + 1, input_box_x + 1, "[1/2] Enter column name");
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

    move(input_box_y + 2, input_box_x + 4);
    getnstr(name, MAX_INPUT - 1);

    // ===============================
    // Step 2: Prompt for Data Type
    // ===============================
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    mvprintw(input_box_y + 1, input_box_x + 1, "[2/2] Enter type for \"%s\" (int, float, str, bool)", name);
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

    move(input_box_y + 2, input_box_x + 4);
    getnstr(type_str, MAX_INPUT - 1);

    // ===============================
    // Add Column
    // ===============================
    DataType type = parse_type_from_string(type_str);
    if (type != TYPE_UNKNOWN) {
        add_column(table, name, type);
    }

    noecho();
    curs_set(0);
}

void prompt_add_row(Table *table) {
    echo();
    curs_set(1);

    char **input_strings = malloc(table->column_count * sizeof(char *));
    int input_box_width = COLS - 4; 
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    for (int i = 0; i < table->column_count; i++) {
        input_strings[i] = malloc(MAX_INPUT);
        const char *col_name = table->columns[i].name;
        const char *col_type = type_to_string(table->columns[i].type);

        // Clear the input box area
        for (int line = 0; line < 5; line++) {
            move(input_box_y + line, 0);
            clrtoeol();
        }

        // Top border
        mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

        // Row 1 (prompt)
        mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
        mvprintw(input_box_y + 1, input_box_x + 1, " [%d/%d] Enter value for \"%s (%s)\"",
                i + 1, table->column_count, col_name, col_type);
        mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

        // Row 2 (input)
        mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
        mvprintw(input_box_y + 2, input_box_x + 1, " > ");
        mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

        // Bottom border
        mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

        // Input position
        move(input_box_y + 2, input_box_x + 4);
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
