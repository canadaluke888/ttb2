#include <ncurses.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "tablecraft.h"
#include "ui.h"
#include "csv.h"
#include "xl.h"
#include "ttb_io.h"
#include "db_manager.h"
#include "errors.h"
#include "panel_manager.h"
#include "db_manager.h"

#define MAX_INPUT 128

static void draw_simple_list_modal(const char *title, const char **items, int count, int *io_selected);
int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty);
static int prompt_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz);
// (no local string-list helpers required here)

static void draw_simple_list_modal(const char *title, const char **items, int count, int *io_selected) {
    int prev_vis = curs_set(0);
    noecho();
    int h = (count + 4); if (h < 8) h = 8;
    int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);
    int selected = (io_selected && *io_selected >= 0 && *io_selected < count) ? *io_selected : 0;
    int ch;
    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "%s", title ? title : "");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        // Title underline
        mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, 2, 0, ACS_LTEE);
        mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
        for (int i = 0; i < count; ++i) {
            int row = 3 + i;
            if (row >= h - 1) break;
            if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, row, 2, "%s", items[i]);
            if (i == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) selected = (selected > 0) ? selected - 1 : count - 1;
        else if (ch == KEY_DOWN) selected = (selected + 1) % count;
        else if (ch == '\n') break;
        else if (ch == 27) { selected = -1; break; }
    }
    if (io_selected) *io_selected = selected;
    pm_remove(modal); pm_remove(shadow); pm_update();
    if (prev_vis != -1) curs_set(prev_vis);
}

int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty)
{
    if (!out || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';
    int len = 0;

    noecho();
    curs_set(1);
    leaveok(stdscr, FALSE);

    int h = 8;
    int w = COLS - 6;
    if (w < 32) w = COLS - 2;
    if (w < 24) w = 24;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    int line_y = 2;
    int prompt_y = 3;
    int input_y = 4;
    int hint_y = h - 2;
    int input_x = 4;
    bool running = true;
    int result = -1;

    while (running) {
        werase(modal->win);
        box(modal->win, 0, 0);

        if (title) {
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "%s", title);
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        }

        // Horizontal divider under title (flush with borders)
        mvwhline(modal->win, line_y, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, line_y, 0, ACS_LTEE);
        mvwaddch(modal->win, line_y, w - 1, ACS_RTEE);

        // Clear interior rows we plan to draw on
        mvwhline(modal->win, prompt_y, 1, ' ', w - 2);
        mvwhline(modal->win, input_y, 1, ' ', w - 2);
        mvwhline(modal->win, hint_y, 1, ' ', w - 2);

        if (prompt && prompt[0]) {
            mvwprintw(modal->win, prompt_y, 2, "%s", prompt);
        }

        if (input_x >= w - 2) {
            input_x = w - 3;
        }
        if (input_x < 2) {
            input_x = 2;
        }

        int field_width = (w - 2) - input_x + 1;
        if (field_width < 0) field_width = 0;

        if (field_width > 0) {
            mvwaddch(modal->win, input_y, 2, '>');
            mvwprintw(modal->win, input_y, input_x, "%.*s", field_width, out);
            if (len < field_width) {
                for (int i = len; i < field_width; ++i) {
                    mvwaddch(modal->win, input_y, input_x + i, ' ');
                }
            }
        }

        if (hint && hint[0]) {
            wattron(modal->win, COLOR_PAIR(4));
            mvwprintw(modal->win, hint_y, 2, "%.*s", w - 4, hint);
            wattroff(modal->win, COLOR_PAIR(4));
        }

        pm_wnoutrefresh(shadow);
        pm_wnoutrefresh(modal);
        pm_update();

        int cursor_x = input_x + len;
        int cursor_max = w - 2;
        if (cursor_x > cursor_max) {
            cursor_x = cursor_max;
        }
        if (cursor_x < input_x) cursor_x = input_x;
        wmove(modal->win, input_y, cursor_x);
        int ch = wgetch(modal->win);
        if (ch == 27) {
            result = -1;
            break;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (len > 0 || allow_empty) {
                result = len;
                break;
            }
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == KEY_DC || ch == 8) {
            if (len > 0) {
                len--;
                out[len] = '\0';
            }
        } else if (ch == KEY_RESIZE) {
            // let caller redraw UI; exit cancel to avoid inconsistent layout
            result = -1;
            break;
        } else if (isprint(ch)) {
            if (len < (int)out_sz - 1) {
                out[len++] = (char)ch;
                out[len] = '\0';
            }
        }
    }

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();

    curs_set(0);
    leaveok(stdscr, TRUE);

    if (result < 0 && out_sz > 0) {
        out[0] = '\0';
    }
    return result;
}

static int prompt_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz)
{
    return show_text_input_modal(title,
                                 "[Enter] Save   [Esc] Cancel",
                                 prompt,
                                 out,
                                 out_sz,
                                 false);
}

static int has_extension(const char *name, const char *ext)
{
    if (!name || !ext) return 0;
    size_t len_name = strlen(name);
    size_t len_ext = strlen(ext);
    if (len_ext > len_name) return 0;
    return strcasecmp(name + len_name - len_ext, ext) == 0;
}

void prompt_add_column(Table *table) {
    char name[MAX_INPUT];
    int name_len = show_text_input_modal("Add Column",
                                     "[Enter] Next   [Esc] Cancel",
                                     "Name:",
                                     name,
                                     sizeof(name),
                                     false);
    if (name_len < 0) {
        return;
    }

    const char *type_items[] = { "int", "float", "str", "bool" };
    int selected = 0;
    draw_simple_list_modal("[2/2] Select column type", type_items, 4, &selected);

    DataType type = TYPE_UNKNOWN;
    if (selected == 0) type = TYPE_INT;
    else if (selected == 1) type = TYPE_FLOAT;
    else if (selected == 2) type = TYPE_STR;
    else if (selected == 3) type = TYPE_BOOL;
    if (type != TYPE_UNKNOWN) {
        add_column(table, name, type);
        char err[256] = {0};
        db_autosave_table(table, err, sizeof(err));
    }
}

void prompt_add_row(Table *table) {
    if (!table || table->column_count == 0) {
        show_error_message("Add at least one column first.");
        return;
    }

    char **input_strings = calloc(table->column_count, sizeof(char *));
    if (!input_strings) {
        show_error_message("Out of memory");
        return;
    }

    bool cancelled = false;
    for (int i = 0; i < table->column_count; ++i) {
        input_strings[i] = calloc(MAX_INPUT, sizeof(char));
        if (!input_strings[i]) {
            cancelled = true;
            break;
        }

        const char *col_name = table->columns[i].name;
        const char *col_type = type_to_string(table->columns[i].type);
        char prompt_label[160];
        snprintf(prompt_label,
                 sizeof(prompt_label),
                 "[%d/%d] %s (%s):",
                 i + 1,
                 table->column_count,
                 col_name,
                 col_type);

        while (1) {
            int rc = show_text_input_modal("Add Row",
                                       "[Enter] Accept   [Esc] Cancel",
                                       prompt_label,
                                       input_strings[i],
                                       MAX_INPUT,
                                       false);
            if (rc < 0) {
                cancelled = true;
                break;
            }
            if (validate_input(input_strings[i], table->columns[i].type)) {
                break;
            }
            show_error_message("Invalid input.");
            input_strings[i][0] = '\0';
        }

        if (cancelled) {
            break;
        }
    }

    if (!cancelled) {
        add_row(table, (const char **)input_strings);
        char err[256] = {0};
        db_autosave_table(table, err, sizeof(err));
    }

    for (int i = 0; i < table->column_count; ++i) {
        free(input_strings[i]);
    }
    free(input_strings);
}

void prompt_rename_table(Table *table) {
    if (!table) {
        return;
    }

    char prompt_label[160];
    const char *current_name = table->name ? table->name : "table";
    snprintf(prompt_label, sizeof(prompt_label), "New name for %s:", current_name);

    char name[128];
    int rc = show_text_input_modal("Rename Table",
                               "[Enter] Save   [Esc] Cancel",
                               prompt_label,
                               name,
                               sizeof(name),
                               false);
    if (rc < 0) {
        return;
    }

    if (strlen(name) > 0) {
        free(table->name);
        table->name = strdup(name);
        char err[256] = {0};
        db_autosave_table(table, err, sizeof(err));
    }
}

void show_table_menu(Table *table) {
    /* Menu uses key navigation only: hide cursor */
    noecho();
    curs_set(0);

    int options_count = 7;
    int h = options_count + 4; /* title underline + options */
    if (h < 8) h = 8; /* minimum height for comfort */
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    const char *labels[] = {"Rename", "Export", "Open File", "New Table", "DB Manager", "Settings", "Cancel"};
    int selected = 0; /* 0=Rename,1=Export,2=Open,3=New,4=DB,5=Settings,6=Cancel */
    int ch;

    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Table Menu:");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, 2, 0, ACS_LTEE);
        mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

        for (int i = 0; i < options_count; i++) {
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
        if (ch == KEY_UP) {
            selected = (selected > 0) ? selected - 1 : options_count - 1;
        } else if (ch == KEY_DOWN) {
            selected = (selected + 1) % options_count;
        } else if (ch == '\n') {
            break;
        } else if (ch == 27) { /* Esc */
            selected = options_count - 1; /* Cancel */
            break;
        }
    }

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();
    noecho();
    curs_set(0);

    switch (selected) {
        case 0: prompt_rename_table(table); break;
        case 1: show_export_menu(table); break;
        case 2: show_open_file(table); break;
        case 3: {
            // New Table: ensure current table saved, then clear to start fresh
            DbManager *cur = db_get_active();
            if (cur && db_is_connected(cur) && table->column_count > 0) {
                char err[256] = {0};
                db_save_table(cur, table, err, sizeof(err));
            }
            // Confirm only if not connected or autosave is off
            if (!cur || !db_is_connected(cur) || !db_autosave_enabled()) {
                int h = 5; int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
                PmNode *sh = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
                PmNode *mo = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
                box(mo->win, 0, 0);
                wattron(mo->win, COLOR_PAIR(3) | A_BOLD);
                mvwprintw(mo->win, 1, 2, "Start a new table? Unsaved changes may be lost.");
                wattroff(mo->win, COLOR_PAIR(3) | A_BOLD);
                wattron(mo->win, COLOR_PAIR(4)); mvwprintw(mo->win, 2, 2, "[Enter] Yes   [Esc] No"); wattroff(mo->win, COLOR_PAIR(4));
                pm_wnoutrefresh(sh); pm_wnoutrefresh(mo); pm_update();
                int c = wgetch(mo->win);
                pm_remove(mo); pm_remove(sh); pm_update();
                if (c == 27) break; // cancel new table
            }
            // free existing contents
            int old_cols = table->column_count;
            int old_rows = table->row_count;
            for (int i = 0; i < old_cols; i++) { if (table->columns[i].name) free(table->columns[i].name); }
            free(table->columns); table->columns = NULL; table->column_count = 0; table->capacity_columns = 0;
            for (int i = 0; i < old_rows; i++) {
                if (table->rows[i].values) {
                    for (int j = 0; j < old_cols; j++) { if (table->rows[i].values[j]) free(table->rows[i].values[j]); }
                    free(table->rows[i].values);
                }
            }
            free(table->rows); table->rows = NULL; table->row_count = 0; table->capacity_rows = 0;
            // rename to Untitled Table
            if (table->name) free(table->name);
            table->name = strdup("Untitled Table");
            cursor_row = -1; cursor_col = 0; col_page = 0;
            break;
        }
        case 4: show_db_manager(table); break;
        case 5: show_settings_menu(); break;
        default: break; /* Cancel */
    }
}

void show_export_menu(Table *table) {
    /* Selection uses keys only: hide cursor */
    noecho();
    curs_set(0);

    /* Modal selection styled like header edit */
    const char *labels[] = {"Table (.ttbl)", "Project (.ttbx)", "CSV", "XLSX", "Cancel"};
    int options_count = 5;
    int h = options_count + 4;
    if (h < 8) h = 8;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    int selected = 0; /* default to first item */
    int ch;
    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Select export format:");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, 2, 0, ACS_LTEE);
        mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

        for (int i = 0; i < options_count; i++) {
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
        else if (ch == 27) { selected = options_count - 1; break; } /* Cancel */
    }

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();
    if (selected == options_count - 1) { /* Cancel */
        noecho();
        curs_set(0);
        return;
    }

    char filename[128];
    int name_len = prompt_filename_modal("Export Table", "Filename:", filename, sizeof(filename));
    if (name_len < 0) {
        noecho();
        curs_set(0);
        return;
    }

    char outpath[512];
    char err[256] = {0};

    if (selected == 0) {
        snprintf(outpath, sizeof(outpath), "%s", filename);
        if (!has_extension(outpath, ".ttbl")) {
            strncat(outpath, ".ttbl", sizeof(outpath) - strlen(outpath) - 1);
        }
        if (ttbl_save(table, outpath, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to export .ttbl");
        } else {
            show_error_message("Exported table file.");
        }
    } else if (selected == 1) {
        snprintf(outpath, sizeof(outpath), "%s", filename);
        if (!has_extension(outpath, ".ttbx")) {
            strncat(outpath, ".ttbx", sizeof(outpath) - strlen(outpath) - 1);
        }
        if (ttbx_save(table, outpath, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to export .ttbx");
        } else {
            show_error_message("Exported project file.");
        }
    } else if (selected == 2) {
        snprintf(outpath, sizeof(outpath), "%s", filename);
        if (!has_extension(outpath, ".csv")) {
            strncat(outpath, ".csv", sizeof(outpath) - strlen(outpath) - 1);
        }
        if (csv_save(table, outpath, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to save CSV");
        } else {
            show_error_message("Exported CSV.");
        }
    } else if (selected == 3) {
        snprintf(outpath, sizeof(outpath), "%s", filename);
        if (!has_extension(outpath, ".xlsx")) {
            strncat(outpath, ".xlsx", sizeof(outpath) - strlen(outpath) - 1);
        }
        if (xl_save(table, outpath, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to save XLSX");
        } else {
            show_error_message("Exported XLSX.");
        }
    }

    clear();
    refresh();
    noecho();
    curs_set(0);

    nodelay(stdscr, TRUE);
}
