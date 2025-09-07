#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "panel_manager.h"
#include "settings.h"
#include "db_manager.h"

int main(void) {
    setlocale(LC_ALL, "");
    initscr();
    pm_init();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    init_colors();  // From ui_init.c
    curs_set(0);
    leaveok(stdscr, TRUE); // avoid moving the hardware cursor unnecessarily

    // Load settings and apply
    AppSettings s; settings_init_defaults(&s); settings_load("settings.json", &s); db_set_autosave_enabled(s.autosave_enabled);

    Table *table = create_table("Untitled Table");
    start_ui_loop(table);  // From ui_loop.c

    free_table(table);
    pm_teardown();
    endwin();
    // Save settings on exit (persist autosave state)
    s.autosave_enabled = db_autosave_enabled();
    settings_save("settings.json", &s);
    return 0;
}
