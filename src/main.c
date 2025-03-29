#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"

int main(void) {
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    init_colors();  // From ui_init.c
    curs_set(0);

    Table *table = create_table("Untitled Table");
    start_ui_loop(table);  // From ui_loop.c

    free_table(table);
    endwin();
    return 0;
}
