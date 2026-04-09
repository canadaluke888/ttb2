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
#include "pdf.h"
#include "ttb_io.h"
#include "db_manager.h"
#include "table_ops.h"
#include "workspace.h"
#include "errors.h"
#include "panel_manager.h"

#define MAX_INPUT 128

typedef enum {
    TABLE_MENU_ACTION_RENAME_TABLE = 0,
    TABLE_MENU_ACTION_RENAME_BOOK,
    TABLE_MENU_ACTION_SORT_ROWS,
    TABLE_MENU_ACTION_FILTER_ROWS,
    TABLE_MENU_ACTION_CLEAR_VIEW,
    TABLE_MENU_ACTION_EXPORT,
    TABLE_MENU_ACTION_OPEN_FILE,
    TABLE_MENU_ACTION_BOOK_TABLES,
    TABLE_MENU_ACTION_NEW_TABLE,
    TABLE_MENU_ACTION_SETTINGS,
    TABLE_MENU_ACTION_BACK
} TableMenuAction;

typedef struct {
    int kind;
    const char *label;
    int action_id;
} TableMenuEntry;

enum {
    TABLE_MENU_ROW_ACTION = 0,
    TABLE_MENU_ROW_HEADING,
    TABLE_MENU_ROW_UNDERLINE,
    TABLE_MENU_ROW_SPACER
};

static int draw_simple_list_modal(const char *title, const char **items, int count, int initial_selected);
static UiMenuResult prompt_rename_active_table(Table *table);
static UiMenuResult prompt_rename_book(void);
static UiMenuResult show_book_tables_page(Table *table);
int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty);
static int prompt_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz);
static int has_extension(const char *name, const char *ext);
// (no local string-list helpers required here)

static int table_menu_next_selectable(const TableMenuEntry *entries, int count, int start, int dir)
{
    int idx = start;

    if (!entries || count <= 0 || dir == 0) return -1;
    for (int step = 0; step < count; ++step) {
        idx += dir;
        if (idx < 0) idx = count - 1;
        else if (idx >= count) idx = 0;
        if (entries[idx].kind == TABLE_MENU_ROW_ACTION) return idx;
    }
    return -1;
}

static int table_menu_find_first_selectable(const TableMenuEntry *entries, int count)
{
    if (!entries) return -1;
    for (int i = 0; i < count; ++i) {
        if (entries[i].kind == TABLE_MENU_ROW_ACTION) return i;
    }
    return -1;
}

static int build_export_path(char *outpath, size_t outpath_sz, const char *dir, const char *filename, const char *ext)
{
    if (!outpath || outpath_sz == 0 || !dir || !*dir || !filename || !*filename || !ext) {
        return -1;
    }

    if (snprintf(outpath, outpath_sz, "%s/%s", dir, filename) >= (int)outpath_sz) {
        return -1;
    }
    if (!has_extension(outpath, ext)) {
        size_t used = strlen(outpath);
        size_t ext_len = strlen(ext);
        if (used + ext_len + 1 > outpath_sz) {
            return -1;
        }
        memcpy(outpath + used, ext, ext_len + 1);
    }
    return 0;
}

static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i) free(list[i]);
    free(list);
}

static int draw_simple_list_modal(const char *title, const char **items, int count, int initial_selected) {
    int prev_vis = curs_set(0);
    noecho();

    int min_h = 8;
    int h = count + 4;
    if (h < min_h) h = min_h;
    int max_h = LINES - 2;
    if (h > max_h) h = max_h;

    int w = COLS - 4;
    if (w < 20) w = COLS - 2;
    if (w < 20) w = 20;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    int selected = (initial_selected >= 0 && initial_selected < count) ? initial_selected : 0;
    if (count <= 0) {
        selected = -1;
    }
    int visible = h - 4;
    if (visible < 1) visible = 1;
    int top = 0;
    if (selected >= visible) {
        top = selected - visible + 1;
    }

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
        int drawn = 0;
        for (; drawn < visible && (top + drawn) < count; ++drawn) {
            int idx = top + drawn;
            int row = 3 + drawn;
            if (idx == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, row, 2, "%s", items[idx]);
            if (idx == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        /* Clear any remaining rows for tidy redraw */
        for (int i = drawn; i < visible; ++i) {
            int row = 3 + i;
            if (row >= h - 1) break;
            mvwhline(modal->win, row, 1, ' ', w - 2);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) {
            if (count > 0) {
                selected = (selected > 0) ? selected - 1 : count - 1;
                if (selected < top) {
                    top = selected;
                }
            }
        }
        else if (ch == KEY_DOWN) {
            if (count > 0) {
                selected = (selected + 1) % count;
                if (selected >= top + visible) {
                    top = selected - visible + 1;
                }
            }
        }
        else if (ch == '\n') break;
        else if (ch == 27) { selected = -1; break; }
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
    if (prev_vis != -1) curs_set(prev_vis);
    return selected;
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
                                 "[Enter] Save   [Esc] Back",
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
    int selected = draw_simple_list_modal("[2/2] Select column type", type_items, 4, 0);
    if (selected < 0) return;

    DataType type = TYPE_UNKNOWN;
    if (selected == 0) type = TYPE_INT;
    else if (selected == 1) type = TYPE_FLOAT;
    else if (selected == 2) type = TYPE_STR;
    else if (selected == 3) type = TYPE_BOOL;
    if (type != TYPE_UNKNOWN) {
        char err[256] = {0};
        if (tableop_insert_column(table, name, type, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to add column.");
        } else {
            ui_rebuild_table_view(table, NULL, 0);
            db_autosave_table(table, err, sizeof(err));
        }
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
        char err[256] = {0};
        if (tableop_insert_row(table, (const char **)input_strings, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to add row.");
        } else {
            ui_rebuild_table_view(table, NULL, 0);
            db_autosave_table(table, err, sizeof(err));
        }
    }

    for (int i = 0; i < table->column_count; ++i) {
        free(input_strings[i]);
    }
    free(input_strings);
}

static UiMenuResult prompt_rename_active_table(Table *table) {
    if (!table) {
        return UI_MENU_BACK;
    }

    char prompt_label[160];
    const char *current_name = table->name ? table->name : "table";
    snprintf(prompt_label, sizeof(prompt_label), "New name for %s:", current_name);

    char name[128];
    int rc = show_text_input_modal("Rename Table",
                               "[Enter] Save   [Esc] Back",
                               prompt_label,
                               name,
                               sizeof(name),
                               false);
    if (rc < 0) {
        return UI_MENU_BACK;
    }

    if (strlen(name) > 0) {
        char err[256] = {0};
        if (workspace_rename_table(table, workspace_active_table_id(), name, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to save renamed table.");
        }
    }
    return UI_MENU_DONE;
}

void prompt_rename_table(Table *table)
{
    (void)prompt_rename_active_table(table);
}

static UiMenuResult prompt_rename_book(void) {
    char prompt_label[192];
    char name[128];
    char err[256] = {0};
    snprintf(prompt_label, sizeof(prompt_label), "New name for %s:", workspace_book_name());
    int rc = show_text_input_modal("Rename Book",
                               "[Enter] Save   [Esc] Back",
                               prompt_label,
                               name,
                               sizeof(name),
                               false);
    if (rc < 0) return UI_MENU_BACK;
    if (workspace_rename_book(name, err, sizeof(err)) != 0) {
        show_error_message(err[0] ? err : "Failed to rename book.");
    }
    return UI_MENU_DONE;
}

static void reset_table_view_state(Table *table)
{
    ui_reset_table_view(table);
}

static UiMenuResult show_book_tables_page(Table *table)
{
    int sel = 0;

    noecho();
    curs_set(0);

    while (1) {
        char **names = NULL, **ids = NULL;
        int count = 0;
        int top = 0;
        char err[256] = {0};

        if (workspace_list_book_tables(&names, &ids, &count, err, sizeof(err)) != 0 || count <= 0) {
            show_error_message(err[0] ? err : "No book tables found.");
            free_string_list(names, count);
            free_string_list(ids, count);
            return UI_MENU_DONE;
        }
        if (sel < 0) sel = 0;
        if (sel >= count) sel = count - 1;

        int h = count + 7;
        if (h < 11) h = 11;
        if (h > LINES - 2) h = LINES - 2;
        int w = COLS - 4;
        int y = (LINES - h) / 2;
        int x = 2;
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(modal->win, TRUE);

        while (1) {
            int visible;
            int ch;
            const char *active_id = workspace_active_table_id();

            werase(modal->win);
            box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "Book Tables: %s", workspace_book_name());
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
            wattron(modal->win, COLOR_PAIR(4));
            mvwprintw(modal->win, h - 2, 2, "[S] Select   [R] Rename   [D] Delete   [Esc] Back");
            wattroff(modal->win, COLOR_PAIR(4));

            visible = h - 5;
            if (visible < 1) visible = 1;
            if (sel < top) top = sel;
            if (sel >= top + visible) top = sel - visible + 1;

            for (int i = 0; i < visible && top + i < count; ++i) {
                int idx = top + i;
                int row = 3 + i;
                int is_active = active_id && ids[idx] && strcmp(active_id, ids[idx]) == 0;
                if (row >= h - 2) break;
                if (idx == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win, row, 2, "%c %s%s",
                          is_active ? '*' : ' ',
                          names[idx] ? names[idx] : "",
                          is_active ? " (active)" : "");
                if (idx == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }

            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            ch = wgetch(modal->win);
            if (ch == KEY_UP) {
                sel = (sel > 0) ? sel - 1 : count - 1;
            } else if (ch == KEY_DOWN) {
                sel = (sel + 1) % count;
            } else if (ch == 's' || ch == 'S' || ch == '\n') {
                if (workspace_switch_table(table, ids[sel], err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Failed to switch table.");
                    err[0] = '\0';
                } else {
                    reset_table_view_state(table);
                    pm_remove(modal);
                    pm_remove(shadow);
                    pm_update();
                    free_string_list(names, count);
                    free_string_list(ids, count);
                    return UI_MENU_DONE;
                }
            } else if (ch == 'r' || ch == 'R') {
                char prompt_label[160];
                char new_name[128];
                snprintf(prompt_label, sizeof(prompt_label), "New name for %s:", names[sel] ? names[sel] : "table");
                int rc = show_text_input_modal("Rename Table",
                                               "[Enter] Save   [Esc] Back",
                                               prompt_label,
                                               new_name,
                                               sizeof(new_name),
                                               false);
                if (rc >= 0) {
                    if (workspace_rename_table(table, ids[sel], new_name, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Failed to rename table.");
                        err[0] = '\0';
                    } else {
                        pm_remove(modal);
                        pm_remove(shadow);
                        pm_update();
                        break;
                    }
                }
            } else if (ch == 'd' || ch == 'D') {
                const char *opts[] = {"No", "Yes"};
                char title[160];
                int pick;
                int was_active = workspace_active_table_id() && ids[sel] &&
                                 strcmp(workspace_active_table_id(), ids[sel]) == 0;
                snprintf(title, sizeof(title), "Delete table '%s'?", names[sel] ? names[sel] : "");
                pick = draw_simple_list_modal(title, opts, 2, 0);
                if (pick == 1) {
                    if (workspace_delete_table(table, ids[sel], err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Failed to delete table.");
                        err[0] = '\0';
                    } else {
                        if (was_active) reset_table_view_state(table);
                        if (sel >= count - 1) sel = count - 2;
                        if (sel < 0) sel = 0;
                        pm_remove(modal);
                        pm_remove(shadow);
                        pm_update();
                        break;
                    }
                }
            } else if (ch == 27) {
                pm_remove(modal);
                pm_remove(shadow);
                pm_update();
                free_string_list(names, count);
                free_string_list(ids, count);
                return UI_MENU_BACK;
            }
        }

        free_string_list(names, count);
        free_string_list(ids, count);
    }
}

void prompt_sort_rows(Table *table)
{
    if (!table || table->column_count <= 0) {
        show_error_message("Add at least one column first.");
        return;
    }
    if (low_ram_mode) {
        show_error_message("Sort is disabled in low-RAM mode.");
        return;
    }

    char **items = calloc((size_t)table->column_count, sizeof(char *));
    const char **labels = calloc((size_t)table->column_count, sizeof(char *));
    int selected_col = 0;
    int selected_order = 0;

    if (!items || !labels) {
        free(items);
        free(labels);
        show_error_message("Out of memory");
        return;
    }

    for (int i = 0; i < table->column_count; ++i) {
        char buf[192];
        snprintf(buf, sizeof(buf), "%s (%s)", table->columns[i].name, type_to_string(table->columns[i].type));
        items[i] = strdup(buf);
        labels[i] = items[i];
        if (!items[i]) {
            free_string_list(items, i);
            free(labels);
            show_error_message("Out of memory");
            return;
        }
    }

    while (1) {
        const char *order_items[] = {"Ascending", "Descending"};

        selected_col = draw_simple_list_modal("Sort Rows By Column", labels, table->column_count, selected_col);
        if (selected_col < 0) break;

        selected_order = draw_simple_list_modal("Sort Order", order_items, 2, selected_order);
        if (selected_order < 0) continue;

        char err[256] = {0};
        if (tableview_sort(table, &ui_table_view, selected_col, selected_order == 1, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to sort rows.");
        } else {
            cursor_row = (ui_visible_row_count(table) > 0) ? 0 : -1;
            cursor_col = 0;
            row_page = 0;
            col_page = 0;
        }
        break;
    }

    free_string_list(items, table->column_count);
    free(labels);
}

void prompt_filter_rows(Table *table)
{
    if (!table || table->column_count <= 0) {
        show_error_message("Add at least one column first.");
        return;
    }
    if (low_ram_mode) {
        show_error_message("Filter is disabled in low-RAM mode.");
        return;
    }

    char **items = calloc((size_t)table->column_count, sizeof(char *));
    const char **labels = calloc((size_t)table->column_count, sizeof(char *));
    int selected_col = 0;
    int selected_op = 0;
    char value[128] = {0};
    FilterRule rule;

    if (!items || !labels) {
        free(items);
        free(labels);
        show_error_message("Out of memory");
        return;
    }

    for (int i = 0; i < table->column_count; ++i) {
        char buf[192];
        snprintf(buf, sizeof(buf), "%s (%s)", table->columns[i].name, type_to_string(table->columns[i].type));
        items[i] = strdup(buf);
        labels[i] = items[i];
        if (!items[i]) {
            free_string_list(items, i);
            free(labels);
            show_error_message("Out of memory");
            return;
        }
    }

    while (1) {
        const char *op_items[] = {"Contains", "Equals", ">", "<", ">=", "<="};
        int restart_column = 0;

        selected_col = draw_simple_list_modal("Filter Rows By Column", labels, table->column_count, selected_col);
        if (selected_col < 0) break;

        while (1) {
            char err[256] = {0};

            selected_op = draw_simple_list_modal("Filter Operator", op_items, 6, selected_op);
            if (selected_op < 0) {
                restart_column = 1;
                break;
            }

            if (show_text_input_modal("Filter Rows",
                                      "[Enter] Apply   [Esc] Back",
                                      "Filter value:",
                                      value,
                                      sizeof(value),
                                      false) < 0) {
                continue;
            }

            memset(&rule, 0, sizeof(rule));
            rule.col = selected_col;
            rule.op = (FilterOp)selected_op;
            strncpy(rule.value, value, sizeof(rule.value) - 1);

            if (tableview_apply_filter(table, &ui_table_view, &rule, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to apply filter.");
                continue;
            }

            cursor_row = (ui_visible_row_count(table) > 0) ? 0 : -1;
            cursor_col = 0;
            row_page = 0;
            col_page = 0;
            restart_column = 0;
            selected_col = -1;
            break;
        }

        if (selected_col < 0) break;
        if (restart_column) continue;
    }

    free_string_list(items, table->column_count);
    free(labels);
}

void clear_table_view_prompt(Table *table)
{
    char err[256] = {0};

    (void)table;
    if (!ui_table_view_is_active()) {
        show_error_message("No active sort or filter.");
        return;
    }
    tableview_clear_filter(&ui_table_view);
    tableview_clear_sort(&ui_table_view);
    if (ui_rebuild_table_view(table, err, sizeof(err)) != 0) {
        show_error_message(err[0] ? err : "Failed to clear view.");
        return;
    }
    cursor_row = -1;
    cursor_col = 0;
    row_page = 0;
    col_page = 0;
}

void show_table_menu(Table *table) {
    int keep_open = 1;

    while (keep_open) {
        const TableMenuEntry entries[] = {
            {TABLE_MENU_ROW_HEADING, "Table", -1},
            {TABLE_MENU_ROW_UNDERLINE, "Table", -1},
            {TABLE_MENU_ROW_ACTION, "Rename Table", TABLE_MENU_ACTION_RENAME_TABLE},
            {TABLE_MENU_ROW_ACTION, "New Table", TABLE_MENU_ACTION_NEW_TABLE},
            {TABLE_MENU_ROW_ACTION, "Book Tables", TABLE_MENU_ACTION_BOOK_TABLES},
            {TABLE_MENU_ROW_SPACER, "", -1},
            {TABLE_MENU_ROW_HEADING, "View", -1},
            {TABLE_MENU_ROW_UNDERLINE, "View", -1},
            {TABLE_MENU_ROW_ACTION, "Sort Rows", TABLE_MENU_ACTION_SORT_ROWS},
            {TABLE_MENU_ROW_ACTION, "Filter Rows", TABLE_MENU_ACTION_FILTER_ROWS},
            {TABLE_MENU_ROW_ACTION, "Clear Sort/Filter", TABLE_MENU_ACTION_CLEAR_VIEW},
            {TABLE_MENU_ROW_SPACER, "", -1},
            {TABLE_MENU_ROW_HEADING, "File", -1},
            {TABLE_MENU_ROW_UNDERLINE, "File", -1},
            {TABLE_MENU_ROW_ACTION, "Export", TABLE_MENU_ACTION_EXPORT},
            {TABLE_MENU_ROW_ACTION, "Open File", TABLE_MENU_ACTION_OPEN_FILE},
            {TABLE_MENU_ROW_SPACER, "", -1},
            {TABLE_MENU_ROW_HEADING, "Workspace", -1},
            {TABLE_MENU_ROW_UNDERLINE, "Workspace", -1},
            {TABLE_MENU_ROW_ACTION, "Rename Book", TABLE_MENU_ACTION_RENAME_BOOK},
            {TABLE_MENU_ROW_ACTION, "Settings", TABLE_MENU_ACTION_SETTINGS},
            {TABLE_MENU_ROW_SPACER, "", -1},
            {TABLE_MENU_ROW_ACTION, "Back to Editor", TABLE_MENU_ACTION_BACK}
        };
        const int entry_count = (int)(sizeof(entries) / sizeof(entries[0]));
        const char *hint = "[↑][↓] Navigate  [Enter] Select  [Esc] Back";
        int body_rows = entry_count + 2; /* blank line before footer + footer hint row */
        int content_w = 0;
        int selected = table_menu_find_first_selectable(entries, entry_count);
        int chosen_action = TABLE_MENU_ACTION_BACK;
        int ch;

        /* Menu uses key navigation only: hide cursor */
        noecho();
        curs_set(0);

        for (int i = 0; i < entry_count; ++i) {
            int len = (int)strlen(entries[i].label);
            if (len > content_w) content_w = len;
        }
        if ((int)strlen(hint) > content_w) content_w = (int)strlen(hint);

        int h = body_rows + 4; /* title underline + body + footer area */
        if (h < 8) h = 8; /* minimum height for comfort */
        if (h > LINES - 2) h = LINES - 2;
        int w = content_w + 8;
        if (w < 34) w = 34;
        if (w > COLS - 4) w = COLS - 4;
        if (w < 20) w = COLS - 2;
        if (w < 20) w = 20;
        int y = (LINES - h) / 2;
        int x = (COLS - w) / 2;

        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(modal->win, TRUE);

        while (1) {
            werase(modal->win);
            box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "Table Menu: %s", workspace_book_name());
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

            for (int i = 0; i < entry_count; i++) {
                int row = 3 + i;
                if (row >= h - 2) break;
                if (entries[i].kind != TABLE_MENU_ROW_ACTION) {
                    if (entries[i].kind == TABLE_MENU_ROW_SPACER) {
                        mvwhline(modal->win, row, 2, ' ', w - 4);
                    } else if (entries[i].kind == TABLE_MENU_ROW_UNDERLINE) {
                        mvwhline(modal->win, row, 2, ACS_HLINE, w - 4);
                    } else {
                        int heading_x = (w - (int)strlen(entries[i].label)) / 2;
                        if (heading_x < 2) heading_x = 2;
                        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
                        mvwprintw(modal->win, row, heading_x, "%s", entries[i].label);
                        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
                    }
                    continue;
                }
                if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win,
                          row,
                          (entries[i].action_id == TABLE_MENU_ACTION_BACK) ? 2 : 4,
                          "%s",
                          entries[i].label);
                if (i == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }

            mvwhline(modal->win, h - 3, 2, ' ', w - 4);
            wattron(modal->win, COLOR_PAIR(4));
            mvwprintw(modal->win, h - 2, 2, "%.*s", w - 4, hint);
            wattroff(modal->win, COLOR_PAIR(4));

            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            ch = wgetch(modal->win);
            if (ch == KEY_UP) {
                int next = table_menu_next_selectable(entries, entry_count, selected, -1);
                if (next >= 0) selected = next;
            } else if (ch == KEY_DOWN) {
                int next = table_menu_next_selectable(entries, entry_count, selected, +1);
                if (next >= 0) selected = next;
            } else if (ch == '\n') {
                if (selected >= 0 && selected < entry_count && entries[selected].kind == TABLE_MENU_ROW_ACTION) {
                    chosen_action = entries[selected].action_id;
                }
                break;
            } else if (ch == 27) { /* Esc */
                chosen_action = TABLE_MENU_ACTION_BACK;
                break;
            }
        }

        pm_remove(modal);
        pm_remove(shadow);
        pm_update();
        noecho();
        curs_set(0);

        switch (chosen_action) {
            case TABLE_MENU_ACTION_RENAME_TABLE:
                if (prompt_rename_active_table(table) == UI_MENU_DONE) keep_open = 0;
                break;
            case TABLE_MENU_ACTION_RENAME_BOOK:
                if (prompt_rename_book() == UI_MENU_DONE) keep_open = 0;
                break;
            case TABLE_MENU_ACTION_SORT_ROWS:
                prompt_sort_rows(table);
                keep_open = 0;
                break;
            case TABLE_MENU_ACTION_FILTER_ROWS:
                prompt_filter_rows(table);
                keep_open = 0;
                break;
            case TABLE_MENU_ACTION_CLEAR_VIEW:
                clear_table_view_prompt(table);
                keep_open = 0;
                break;
            case TABLE_MENU_ACTION_EXPORT:
                if (show_export_menu(table) == UI_MENU_DONE) keep_open = 0;
                break;
            case TABLE_MENU_ACTION_OPEN_FILE:
                if (show_open_file(table) == UI_MENU_DONE) keep_open = 0;
                break;
            case TABLE_MENU_ACTION_BOOK_TABLES:
                if (show_book_tables_page(table) == UI_MENU_DONE) keep_open = 0;
                break;
            case TABLE_MENU_ACTION_NEW_TABLE: {
                if (table->column_count > 0 && !workspace_autosave_enabled()) {
                    int h = 5; int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
                    PmNode *sh = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
                    PmNode *mo = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
                    box(mo->win, 0, 0);
                    wattron(mo->win, COLOR_PAIR(3) | A_BOLD);
                    mvwprintw(mo->win, 1, 2, "Start a new table? Unsaved changes may be lost.");
                    wattroff(mo->win, COLOR_PAIR(3) | A_BOLD);
                    wattron(mo->win, COLOR_PAIR(4)); mvwprintw(mo->win, 2, 2, "[Enter] Yes   [Esc] Back"); wattroff(mo->win, COLOR_PAIR(4));
                    pm_wnoutrefresh(sh); pm_wnoutrefresh(mo); pm_update();
                    int c = wgetch(mo->win);
                    pm_remove(mo); pm_remove(sh); pm_update();
                    if (c == 27) break;
                }
                {
                    char err[256] = {0};
                    if (workspace_new_table(table, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Failed to create table.");
                        break;
                    }
                }
                cursor_row = -1; cursor_col = 0; col_page = 0;
                keep_open = 0;
                break;
            }
            case TABLE_MENU_ACTION_SETTINGS:
                if (show_settings_menu() == UI_MENU_DONE) keep_open = 0;
                break;
            default:
                keep_open = 0;
                break; /* Back to editor */
        }
    }
}

UiMenuResult show_export_menu(Table *table) {
    /* Selection uses keys only: hide cursor */
    while (1) {
        noecho();
        curs_set(0);

        /* Modal selection styled like header edit */
        const char *labels[] = {"Table (.ttbl)", "Book (.ttbx)", "CSV", "XLSX", "PDF", "SQLite DB (.db)", "Back"};
        int options_count = 7;
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
            else if (ch == 27) {
                pm_remove(modal);
                pm_remove(shadow);
                pm_update();
                noecho();
                curs_set(0);
                return UI_MENU_DONE;
            }
        }

        pm_remove(modal);
        pm_remove(shadow);
        pm_update();
        if (selected == options_count - 1) { /* Back */
            noecho();
            curs_set(0);
            return UI_MENU_BACK;
        }

        char directory[512];
        char filename[128];
        char outpath[512];
        char err[256] = {0};

        if (ui_pick_directory(directory, sizeof(directory), "Select Export Directory") != 0) {
            noecho();
            curs_set(0);
            continue;
        }

        int name_len = prompt_filename_modal("Export Table", "Filename:", filename, sizeof(filename));
        if (name_len < 0) {
            noecho();
            curs_set(0);
            continue;
        }

        if (selected == 0) {
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".ttbl") != 0) {
                show_error_message("Export path is too long.");
            } else if (ttbl_save(table, outpath, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to export .ttbl");
            } else {
                show_error_message("Exported table file.");
            }
        } else if (selected == 1) {
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".ttbx") != 0) {
                show_error_message("Export path is too long.");
            } else if (workspace_export_book(outpath, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to export .ttbx");
            } else {
                show_error_message("Exported book.");
            }
        } else if (selected == 2) {
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".csv") != 0) {
                show_error_message("Export path is too long.");
            } else if (csv_save(table, outpath, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to save CSV");
            } else {
                show_error_message("Exported CSV.");
            }
        } else if (selected == 3) {
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".xlsx") != 0) {
                show_error_message("Export path is too long.");
            } else if (xl_save(table, outpath, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to save XLSX");
            } else {
                show_error_message("Exported XLSX.");
            }
        } else if (selected == 4) {
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".pdf") != 0) {
                show_error_message("Export path is too long.");
            } else if (pdf_save(table, outpath, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to save PDF");
            } else {
                show_error_message("Exported PDF.");
            }
        } else if (selected == 5) {
            const char *scope_labels[] = {"Single Table", "Whole Book"};
            int scope_pick = draw_simple_list_modal("Export SQLite DB", scope_labels, 2, 0);
            if (scope_pick < 0) {
                clear();
                refresh();
                noecho();
                curs_set(0);
                nodelay(stdscr, TRUE);
                continue;
            }
            if (build_export_path(outpath, sizeof(outpath), directory, filename, ".db") != 0) {
                show_error_message("Export path is too long.");
            } else if ((scope_pick == 0 && db_export_table_path(table, outpath, err, sizeof(err)) != 0) ||
                (scope_pick == 1 && workspace_export_book_db(outpath, err, sizeof(err)) != 0)) {
                show_error_message(err[0] ? err : "Failed to export SQLite DB");
            } else {
                show_error_message(scope_pick == 0 ? "Exported SQLite DB for table." : "Exported SQLite DB for book.");
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
