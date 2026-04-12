/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Export dialogs for CSV, XLSX, PDF, and workspace outputs. */

#include <ncurses.h>
#include "ui/internal.h"
#include "io/csv.h"
#include "io/xl.h"
#include "io/pdf.h"
#include "io/ttb_io.h"
#include "db/db_manager.h"
#include "core/workspace.h"
#include "core/errors.h"
#include "ui/panel_manager.h"
#include "ui/dialog_internal.h"

UiMenuResult show_export_menu(Table *table)
{
    while (1) {
        const char *labels[] = {"Table (.ttbl)", "Book (.ttbx SQLite)", "CSV", "XLSX", "PDF", "SQLite DB (.db)", "Back"};
        int options_count = 7;
        int h = options_count + 4;
        int w = COLS - 4;
        int y;
        int x = 2;
        int selected = 0;
        int ch;

        noecho();
        curs_set(0);

        if (h < 8) h = 8;
        y = (LINES - h) / 2;

        {
            PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
            PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);

            keypad(modal->win, TRUE);
            while (1) {
                werase(modal->win);
                box(modal->win, 0, 0);
                wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
                mvwprintw(modal->win, 1, 2, "Select export format:");
                wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
                mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
                mvwaddch(modal->win, 2, 0, ACS_LTEE);
                mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

                for (int i = 0; i < options_count; ++i) {
                    int row = 3 + i;

                    if (row >= h - 1) break;
                    if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                    mvwprintw(modal->win, row, 2, "%s", labels[i]);
                    if (i == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
                }

                pm_wnoutrefresh(shadow);
                pm_wnoutrefresh(modal);
                pm_update();

                ch = wgetch(modal->win);
                if (ch == KEY_UP) selected = (selected > 0) ? selected - 1 : options_count - 1;
                else if (ch == KEY_DOWN) selected = (selected + 1) % options_count;
                else if (ch == '\n') break;
                else if (ch == 27) {
                    pm_remove(modal);
                    pm_remove(shadow);
                    pm_update();
                    return UI_MENU_DONE;
                }
            }

            pm_remove(modal);
            pm_remove(shadow);
            pm_update();
        }

        if (selected == options_count - 1) return UI_MENU_BACK;

        {
            char directory[512];
            char filename[128];
            char outpath[512];
            char err[256] = {0};

            if (ui_pick_directory(directory, sizeof(directory), "Select Export Directory") != 0) continue;
            if (ui_dialog_show_save_filename_modal("Export Table", "Filename:", filename, sizeof(filename)) < 0) continue;

            if (selected == 0) {
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".ttbl") != 0) {
                    show_error_message("Export path is too long.");
                } else if (ttbl_save(table, outpath, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to export .ttbl");
                } else {
                    show_error_message("Exported table file.");
                }
            } else if (selected == 1) {
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".ttbx") != 0) {
                    show_error_message("Export path is too long.");
                } else if (workspace_export_book(outpath, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to export .ttbx");
                } else {
                    show_error_message("Exported book.");
                }
            } else if (selected == 2) {
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".csv") != 0) {
                    show_error_message("Export path is too long.");
                } else if (csv_save(table, outpath, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to save CSV");
                } else {
                    show_error_message("Exported CSV.");
                }
            } else if (selected == 3) {
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".xlsx") != 0) {
                    show_error_message("Export path is too long.");
                } else if (xl_save(table, outpath, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to save XLSX");
                } else {
                    show_error_message("Exported XLSX.");
                }
            } else if (selected == 4) {
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".pdf") != 0) {
                    show_error_message("Export path is too long.");
                } else if (pdf_save(table, outpath, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to save PDF");
                } else {
                    show_error_message("Exported PDF.");
                }
            } else if (selected == 5) {
                const char *scope_labels[] = {"Single Table", "Whole Book"};
                int scope_pick = ui_dialog_show_simple_list_modal("Export SQLite DB", scope_labels, 2, 0);

                if (scope_pick < 0) {
                    clear();
                    refresh();
                    noecho();
                    curs_set(0);
                    nodelay(stdscr, TRUE);
                    continue;
                }
                if (ui_dialog_build_output_path(outpath, sizeof(outpath), directory, filename, ".db") != 0) {
                    show_error_message("Export path is too long.");
                } else if ((scope_pick == 0 && db_export_table_path(table, outpath, err, sizeof(err)) != 0) ||
                           (scope_pick == 1 && workspace_export_book_db(outpath, err, sizeof(err)) != 0)) {
                    show_error_message(err[0] ? err : "Failed to export SQLite DB");
                } else {
                    show_error_message(scope_pick == 0 ? "Exported SQLite DB for table." : "Exported SQLite DB for book.");
                }
            }
        }

        clear();
        refresh();
        noecho();
        curs_set(0);
        nodelay(stdscr, TRUE);
        return UI_MENU_DONE;
    }
}
