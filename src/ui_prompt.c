#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message

#define MAX_INPUT 128

void prompt_add_column(Table *table) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    char type_str[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    // Step 1: Prompt for Column Name
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }
    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);
    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, "[1/2] Enter column name");
    attroff(COLOR_PAIR(3) | A_BOLD);
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);
    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    attroff(COLOR_PAIR(4));
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);
    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);
    move(input_box_y + 2, input_box_x + 4);
    getnstr(name, MAX_INPUT - 1);

    // Step 2: Prompt for Data Type
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }
    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);
    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, "[2/2] Enter type for \"%s\" (int, float, str, bool)", name);
    attroff(COLOR_PAIR(3) | A_BOLD);
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);
    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    attroff(COLOR_PAIR(4));
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);
    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);
    move(input_box_y + 2, input_box_x + 4);
    getnstr(type_str, MAX_INPUT - 1);

    DataType type = parse_type_from_string(type_str);
    if (type != TYPE_UNKNOWN)
        add_column(table, name, type);

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

        while (1) {
            for (int line = 0; line < 6; line++) {
                move(input_box_y + line, 0);
                clrtoeol();
            }
            mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
            for (int j = 0; j < input_box_width - 1; j++)
                mvaddch(input_box_y, input_box_x + j + 1, ACS_HLINE);
            mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);
            mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(input_box_y + 1, input_box_x + 1, " [%d/%d] Enter value for \"%s (%s)\"",
                     i + 1, table->column_count, col_name, col_type);
            attroff(COLOR_PAIR(3) | A_BOLD);
            mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);
            mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
            attron(COLOR_PAIR(4));
            mvprintw(input_box_y + 2, input_box_x + 1, " > ");
            attroff(COLOR_PAIR(4));
            mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);
            mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
            for (int j = 0; j < input_box_width - 1; j++)
                mvaddch(input_box_y + 3, input_box_x + j + 1, ACS_HLINE);
            mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);
            move(input_box_y + 2, input_box_x + 4);
            getnstr(input_strings[i], MAX_INPUT - 1);

            if (validate_input(input_strings[i], table->columns[i].type))
                break;
            else
                show_error_message("Invalid input.");
        }
    }

    add_row(table, (const char **)input_strings);

    for (int i = 0; i < table->column_count; i++) {
        free(input_strings[i]);
    }
    free(input_strings);

    noecho();
    curs_set(0);
}
