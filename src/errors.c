#include <ncurses.h>
#include <string.h>
#include "../include/errors.h"

void show_error_message(const char *msg) {
    int box_width = COLS - 4;
    int box_x = 2;
    int box_y = LINES / 2 - 2;

    echo();
    curs_set(0);

    for (int line = 0; line < 5; line++) {
        move(box_y + line, 0);
        clrtoeol();
    }

    // Draw border
    mvaddch(box_y, box_x, ACS_ULCORNER);
    for (int i = 1; i < box_width - 1; i++)
        mvaddch(box_y, box_x + i, ACS_HLINE);
    mvaddch(box_y, box_x + box_width - 1, ACS_URCORNER);

    mvaddch(box_y + 1, box_x, ACS_VLINE);
    attron(COLOR_PAIR(10) | A_BOLD);
    mvprintw(box_y + 1, box_x + 2, "%s", msg);
    attroff(COLOR_PAIR(10) | A_BOLD);
    mvaddch(box_y + 1, box_x + box_width - 1, ACS_VLINE);

    mvaddch(box_y + 2, box_x, ACS_VLINE);
    attron(COLOR_PAIR(11));
    mvprintw(box_y + 2, box_x + 2, "(Press any key to continue)");
    attroff(COLOR_PAIR(11));
    mvaddch(box_y + 2, box_x + box_width - 1, ACS_VLINE);

    mvaddch(box_y + 3, box_x, ACS_LLCORNER);
    for (int i = 1; i < box_width - 1; i++)
        mvaddch(box_y + 3, box_x + i, ACS_HLINE);
    mvaddch(box_y + 3, box_x + box_width - 1, ACS_LRCORNER);

    refresh();
    getch();

    // Clear it
    for (int line = 0; line < 5; line++) {
        move(box_y + line, 0);
        clrtoeol();
    }

    noecho();
}