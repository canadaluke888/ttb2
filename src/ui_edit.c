#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message

#define MAX_INPUT 128

void edit_header_cell(Table *t, int col) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

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
    mvprintw(input_box_y + 1, input_box_x + 1, " Rename column \"%s\":", t->columns[col].name);
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

    if (strlen(name) > 0) {
        free(t->columns[col].name);
        t->columns[col].name = strdup(name);
    }

    noecho();
    curs_set(0);
}

void edit_body_cell(Table *t, int row, int col) {
    echo();
    curs_set(1);

    char value[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;
    const char *col_name = t->columns[col].name;
    const char *type_str = type_to_string(t->columns[col].type);

    while (1) {
        for (int line = 0; line < 6; line++) {
            move(input_box_y + line, 0);
            clrtoeol();
        }

        // Top border
        mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

        // Prompt line
        mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(input_box_y + 1, input_box_x + 1, " Edit value for \"%s (%s)\"", col_name, type_str);
        attroff(COLOR_PAIR(3) | A_BOLD);
        mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

        // Input line
        mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
        attron(COLOR_PAIR(4));
        mvprintw(input_box_y + 2, input_box_x + 1, " > ");
        attroff(COLOR_PAIR(4));
        mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

        // Bottom border
        mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

        move(input_box_y + 2, input_box_x + 4);
        getnstr(value, MAX_INPUT - 1);

        if (!validate_input(value, t->columns[col].type)) {
            show_error_message("Invalid input.");
            continue;
        }

        void *ptr = NULL;
        switch (t->columns[col].type) {
            case TYPE_INT: {
                int *i = malloc(sizeof(int));
                *i = atoi(value);
                ptr = i;
                break;
            }
            case TYPE_FLOAT: {
                float *f = malloc(sizeof(float));
                *f = atof(value);
                ptr = f;
                break;
            }
            case TYPE_BOOL: {
                int *b = malloc(sizeof(int));
                *b = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
                ptr = b;
                break;
            }
            case TYPE_STR: {
                ptr = strdup(value);
                break;
            }
            default:
                ptr = NULL;
                break;
        }

        if (t->rows[row].values[col])
            free(t->rows[row].values[col]);
        t->rows[row].values[col] = ptr;
        break; // Valid input complete
    }

    noecho();
    curs_set(0);
}
