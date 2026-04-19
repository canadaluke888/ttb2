/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* File-picker and path-selection dialogs for opening and saving data. */

#include <ncurses.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ui/panel_manager.h"
#include "core/errors.h"
#include "core/settings.h"
#include "io/csv.h"
#include "io/xl.h"
#include "io/ttb_io.h"
#include "core/workspace.h"
#include "ui/internal.h"
#include "ui/ui_loading.h"
#include "ui/dialog_internal.h"

typedef struct {
    char *name;
    int is_dir;
} Entry;

/* Sort directory entries with folders first and names in lexical order. */
static int entry_cmp(const void *a, const void *b) {
    const Entry *ea = (const Entry*)a, *eb = (const Entry*)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir; // dirs first
    return strcmp(ea->name, eb->name);
}

static Entry *list_dir(const char *path, int *out_count) {
    DIR *d = opendir(path);
    if (!d) { *out_count = 0; return NULL; }
    int cap = 32, n = 0; Entry *arr = (Entry*)malloc(sizeof(Entry) * cap);
    // Add parent entry
    arr[n].name = strdup(".."); arr[n].is_dir = 1; n++;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        // skip hidden by default
        if (nm[0] == '.') continue;
        if (n == cap) { cap *= 2; arr = (Entry*)realloc(arr, sizeof(Entry) * cap); }
        arr[n].name = strdup(nm);
        arr[n].is_dir = (de->d_type == DT_DIR);
        n++;
    }
    closedir(d);
    qsort(arr, n, sizeof(Entry), entry_cmp);
    *out_count = n;
    return arr;
}

/* Free the directory listing used by the file and directory pickers. */
static void free_entries(Entry *arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; ++i) free(arr[i].name);
    free(arr);
}

static void replace_loaded_table(Table *table, Table *loaded)
{
    if (!table || !loaded) return;
    replace_table_contents(table, loaded);
    workspace_set_active_table(table);
    ui_reset_table_view(table);
}

static int prepare_for_single_table_open(Table *table)
{
    if (!table) return 0;

    if ((table->column_count > 0 || table->row_count > 0) &&
        !workspace_autosave_enabled()) {
        int h = 5;
        int w = COLS - 4;
        int y = (LINES - h) / 2;
        int x = 2;
        PmNode *sh = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *mo = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        box(mo->win, 0, 0);
        wattron(mo->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(mo->win, 1, 2, "Open another file? Current table will be saved first.");
        wattroff(mo->win, COLOR_PAIR(3) | A_BOLD);
        wattron(mo->win, COLOR_PAIR(4));
        mvwprintw(mo->win, 2, 2, "[Enter] Continue   [Esc] Cancel");
        wattroff(mo->win, COLOR_PAIR(4));
        pm_wnoutrefresh(sh);
        pm_wnoutrefresh(mo);
        pm_update();
        int c = wgetch(mo->win);
        pm_remove(mo);
        pm_remove(sh);
        pm_update();
        if (c == 27) return -1;
    }

    {
        char err[256] = {0};
        if (workspace_new_table(table, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to prepare new table.");
            return -1;
        }
    }

    return 0;
}

int ui_open_path(Table *table, const char *path, int preserve_current_table, int show_book_success)
{
    struct stat st;
    int is_csv;
    int is_xlsx;
    int is_ttbl;
    int is_ttbx;
    AppSettings s;
    char err[256] = {0};
    Table *loaded = NULL;
    UiLoadingModal *loading_modal = NULL;
    ProgressReporter reporter = {0};

    if (!table || !path || !*path) {
        show_error_message("No file path provided.");
        return -1;
    }

    if (stat(path, &st) != 0) {
        show_error_message("Failed to read selected path.");
        return -1;
    }

    is_csv = S_ISREG(st.st_mode) && ui_dialog_path_has_extension(path, ".csv");
    is_xlsx = S_ISREG(st.st_mode) && ui_dialog_path_has_extension(path, ".xlsx");
    is_ttbl = S_ISREG(st.st_mode) && ui_dialog_path_has_extension(path, ".ttbl");
    is_ttbx = ttbx_is_book_dir(path) || ui_dialog_path_has_extension(path, ".ttbx");

    if (!is_csv && !is_xlsx && !is_ttbl && !is_ttbx) {
        show_error_message("Unsupported file type.");
        return -1;
    }

    if (!is_ttbx && preserve_current_table && prepare_for_single_table_open(table) != 0) {
        return -1;
    }

    settings_init_defaults(&s);
    settings_load(settings_default_path(), &s);

    if (is_csv) {
        loading_modal = ui_loading_modal_start("Import CSV", "Reading file...", &reporter);
        const ProgressReporter *cb = (loading_modal && reporter.update) ? &reporter : NULL;
        loaded = csv_load_with_progress(path, s.type_infer_enabled, err, sizeof(err), cb);
    } else if (is_xlsx) {
        loading_modal = ui_loading_modal_start("Import XLSX", "Parsing workbook...", &reporter);
        const ProgressReporter *cb = (loading_modal && reporter.update) ? &reporter : NULL;
        loaded = xl_load_with_progress(path, s.type_infer_enabled, err, sizeof(err), cb);
    } else if (is_ttbl) {
        loaded = ttbl_load(path, err, sizeof(err));
    } else if (workspace_open_book(table, path, err, sizeof(err)) == 0) {
        workspace_set_active_table(table);
        ui_reset_table_view(table);
    }

    if (loading_modal) {
        ui_loading_modal_finish(loading_modal);
    }

    if (is_ttbx) {
        if (err[0]) {
            show_error_message(err);
            return -1;
        }
        return 0;
    }

    if (!loaded) {
        show_error_message(err[0] ? err : "Failed to load file");
        return -1;
    }

    replace_loaded_table(table, loaded);
    if (workspace_manual_save(table, err, sizeof(err)) != 0) {
        show_error_message(err[0] ? err : "Failed to save opened file.");
        return -1;
    }
    return 0;
}

int ui_pick_directory(char *out, size_t out_sz, const char *title)
{
    char cwd[1024];
    int sel = 0;

    if (!out || out_sz == 0 || !getcwd(cwd, sizeof(cwd))) {
        return -1;
    }

    while (1) {
        int count = 0;
        Entry *ents = list_dir(cwd, &count);
        if (!ents) {
            show_error_message("Failed to read directory.");
            return -1;
        }

        int h = count + 7;
        if (h < 11) h = 11;
        if (h > LINES - 2) h = LINES - 2;
        int w = COLS - 4;
        int y = (LINES - h) / 2;
        int x = 2;
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(modal->win, TRUE);
        int top = 0;

        while (1) {
            int visible;
            int ch;

            werase(modal->win);
            box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "%s - %s", title ? title : "Select Directory", cwd);
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
            wattron(modal->win, COLOR_PAIR(4));
            mvwprintw(modal->win, h - 2, 2, "[Enter] Open/Choose   [S] Select this directory   [Esc] Back");
            wattroff(modal->win, COLOR_PAIR(4));
            visible = h - 5;
            if (visible < 1) visible = 1;
            if (sel >= visible) top = sel - (visible - 1);
            for (int i = 0; i < visible && top + i < count; ++i) {
                int idx = top + i;
                int row = 3 + i;
                if (row >= h - 2) break;
                if (idx == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win, row, 2, "%s%s", ents[idx].name, ents[idx].is_dir ? "/" : "");
                if (idx == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }
            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            ch = wgetch(modal->win);
            if (ch == KEY_MOUSE) {
                int activate = 0;
                int nav_dir = 0;

                if (ui_dialog_handle_list_mouse(modal->win, ch, 3, visible, count, &top, &sel, &activate, &nav_dir)) {
                    if (activate) break;
                    continue;
                }
            } else if (ch == KEY_UP) {
                sel = (sel > 0) ? sel - 1 : count - 1;
                if (sel < top) top = sel;
            } else if (ch == KEY_DOWN) {
                sel = (sel + 1) % count;
                if (sel >= top + visible) top = sel - visible + 1;
            } else if (ch == 's' || ch == 'S') {
                strncpy(out, cwd, out_sz - 1);
                out[out_sz - 1] = '\0';
                pm_remove(modal);
                pm_remove(shadow);
                pm_update();
                free_entries(ents, count);
                return 0;
            } else if (ch == '\n') {
                break;
            } else if (ch == 27) {
                pm_remove(modal);
                pm_remove(shadow);
                pm_update();
                free_entries(ents, count);
                return -1;
            }
        }

        if (strcmp(ents[sel].name, "..") == 0) {
            char *slash = strrchr(cwd, '/');
            if (slash && slash != cwd) {
                *slash = '\0';
            } else {
                strcpy(cwd, "/");
            }
            sel = 0;
        } else {
            char path[1536];
            struct stat st;

            snprintf(path, sizeof(path), "%s/%s", cwd, ents[sel].name);
            if (stat(path, &st) != 0) {
                show_error_message("Stat failed.");
                pm_remove(modal);
                pm_remove(shadow);
                pm_update();
                free_entries(ents, count);
                return -1;
            }
            if (S_ISDIR(st.st_mode) && !ttbx_is_book_dir(path)) {
                strncpy(cwd, path, sizeof(cwd) - 1);
                cwd[sizeof(cwd) - 1] = '\0';
                sel = 0;
            } else if (S_ISDIR(st.st_mode)) {
                show_error_message("Select a destination directory, not a book.");
            } else {
                show_error_message("Select a directory.");
            }
        }

        pm_remove(modal);
        pm_remove(shadow);
        pm_update();
        free_entries(ents, count);
    }
}

/* Show the file picker used to open CSV, XLSX, TTBL, and TTBX inputs. */
UiMenuResult show_open_file(Table *table) {
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    int sel = 0;
    while (1) {
        int count = 0; Entry *ents = list_dir(cwd, &count);
        if (!ents) { show_error_message("Failed to read directory."); return UI_MENU_DONE; }
        int h = (count + 7); if (h < 11) h = 11; if (h > LINES - 2) h = LINES - 2;
        int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(modal->win, TRUE);
        int top = 0;

        int ch;
        while (1) {
            werase(modal->win); box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "Open File - %s", cwd);
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
            wattron(modal->win, COLOR_PAIR(4));
            mvwprintw(modal->win, h - 2, 2, "[Enter] Select/Open   [Esc] Back");
            wattroff(modal->win, COLOR_PAIR(4));
            int visible = h - 5; if (visible < 1) visible = 1;
            if (sel >= visible) top = sel - (visible - 1);
            for (int i = 0; i < visible && top + i < count; ++i) {
                int idx = top + i;
                int row = 3 + i;
                if (row >= h - 2) break;
                if (idx == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win, row, 2, "%s%s", ents[idx].name, ents[idx].is_dir ? "/" : "");
                if (idx == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }
            pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
            ch = wgetch(modal->win);
            if (ch == KEY_MOUSE) {
                int activate = 0;
                int nav_dir = 0;

                if (ui_dialog_handle_list_mouse(modal->win, ch, 3, visible, count, &top, &sel, &activate, &nav_dir)) {
                    if (activate) break;
                    continue;
                }
            } else if (ch == KEY_UP) { sel = (sel > 0) ? sel - 1 : count - 1; if (sel < top) top = sel; }
            else if (ch == KEY_DOWN) { sel = (sel + 1) % count; if (sel >= top + visible) top = sel - visible + 1; }
            else if (ch == KEY_LEFT) { sel = 0; ch = 10; /* treat as enter on '..' */ }
            else if (ch == '\n') break;
            else if (ch == 27) { pm_remove(modal); pm_remove(shadow); pm_update(); free_entries(ents, count); return UI_MENU_BACK; }
        }

        // Handle selection
        char path[1536];
        if (strcmp(ents[sel].name, "..") == 0) {
            // go up
            char *slash = strrchr(cwd, '/');
            if (slash && slash != cwd) { *slash = '\0'; }
            else strcpy(cwd, "/");
            pm_remove(modal); pm_remove(shadow); pm_update();
            free_entries(ents, count);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", cwd, ents[sel].name);
        struct stat st; if (stat(path, &st) != 0) { show_error_message("Stat failed."); pm_remove(modal); pm_remove(shadow); pm_update(); free_entries(ents, count); return UI_MENU_DONE; }
        if (S_ISDIR(st.st_mode) && !ttbx_is_book_dir(path)) {
            // enter directory
            strncpy(cwd, path, sizeof(cwd) - 1); cwd[sizeof(cwd) - 1] = '\0'; sel = 0;
            pm_remove(modal); pm_remove(shadow); pm_update();
            free_entries(ents, count);
            continue;
        }

        // Close the picker before loading the selected file/book so the old panel
        // does not remain visible during the open operation.
        pm_remove(modal);
        pm_remove(shadow);
        pm_update();

        ui_open_path(table, path, 1, 1);
        free_entries(ents, count);
        return UI_MENU_DONE;
    }
}
