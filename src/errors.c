#include <ncurses.h>
#include <string.h>
#include "../include/errors.h"
#include "panel_manager.h"

void show_error_message(const char *msg) {
    int h = 4;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;

    echo();
    curs_set(0);

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);

    box(modal->win, 0, 0);
    wattron(modal->win, COLOR_PAIR(10) | A_BOLD);
    mvwprintw(modal->win, 1, 2, "%s", msg);
    wattroff(modal->win, COLOR_PAIR(10) | A_BOLD);
    wattron(modal->win, COLOR_PAIR(11));
    mvwprintw(modal->win, 2, 2, "(Press any key to continue)");
    wattroff(modal->win, COLOR_PAIR(11));

    pm_wnoutrefresh(shadow);
    pm_wnoutrefresh(modal);
    pm_update();
    getch();

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();

    noecho();
}