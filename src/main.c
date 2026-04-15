/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Application entry point and high-level runtime initialization. */

#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include "data/table.h"
#include "ui/ui.h"
#include "ui/panel_manager.h"
#include "core/settings.h"
#include "db/db_manager.h"
#include "core/workspace.h"
#include "core/errors.h"

/* Initialize ncurses, restore workspace state, and run the main editor loop. */
int main(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [file-or-book]\n", argv[0]);
        return 1;
    }

    setlocale(LC_ALL, "");
    initscr();
    set_escdelay(25);
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
    ui_set_low_ram_mode(s.low_ram_enabled);
    ui_set_row_gutter_enabled(s.show_row_gutter);

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

    if (argc == 2 && table) {
        ui_open_path(table, argv[1], 0, 0);
    }

    start_ui_loop(table);  // From ui_loop.c

    workspace_flush_autosave(NULL, 0);
    free_table(table);
    workspace_shutdown();
    pm_teardown();
    endwin();
    // Save settings on exit (persist runtime toggles)
    settings_init_defaults(&s);
    settings_load(settings_default_path(), &s);
    s.autosave_enabled = workspace_autosave_enabled();
    s.low_ram_enabled = ui_low_ram_mode_enabled();
    s.show_row_gutter = ui_row_gutter_enabled();
    settings_save(settings_default_path(), &s);
    return 0;
}
