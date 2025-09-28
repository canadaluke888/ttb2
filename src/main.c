#include <locale.h>
#include <ncurses.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "panel_manager.h"
#include "settings.h"
#include "db_manager.h"
#include "workspace.h"
#include "errors.h"

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
    AppSettings s;
    settings_init_defaults(&s);
    settings_ensure_directory();
    settings_load(settings_default_path(), &s);
    workspace_set_autosave_enabled(s.autosave_enabled);
    low_ram_mode = s.low_ram_enabled ? 1 : 0;
    row_gutter_enabled = s.show_row_gutter ? 1 : 0;

    Table *table = NULL;
    char werr[256] = {0};
    if (workspace_init(&table, werr, sizeof(werr)) != 0 || !table) {
        table = create_table("Untitled Table");
        workspace_set_active_table(table);
        workspace_autosave(table, NULL, 0);
        if (werr[0]) {
            show_error_message(werr);
        }
    }

    start_ui_loop(table);  // From ui_loop.c

    workspace_manual_save(table, NULL, 0);
    free_table(table);
    pm_teardown();
    endwin();
    // Save settings on exit (persist runtime toggles)
    settings_init_defaults(&s);
    settings_load(settings_default_path(), &s);
    s.autosave_enabled = workspace_autosave_enabled();
    s.low_ram_enabled = (low_ram_mode != 0);
    s.show_row_gutter = (row_gutter_enabled != 0);
    settings_save(settings_default_path(), &s);
    return 0;
}
