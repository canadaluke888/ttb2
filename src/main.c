#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include "../include/tablecraft.h"

void start_ui_loop(Table *table);
void init_colors(void);

int main(void) {
    setlocale(LC_ALL, "");

    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    init_colors();
    curs_set(0);

    Table *table = create_table("Untitled Table");
    start_ui_loop(table);

    free_table(table);
    endwin();
    return 0;
}
