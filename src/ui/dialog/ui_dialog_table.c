#include <ncurses.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "data/table.h"
#include "ui/internal.h"
#include "db/db_manager.h"
#include "data/table_ops.h"
#include "core/workspace.h"
#include "core/errors.h"
#include "ui/panel_manager.h"
#include "ui/ui_history.h"
#include "ui/dialog_internal.h"

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

static UiMenuResult prompt_rename_active_table(Table *table);
static UiMenuResult prompt_rename_book(void);
static UiMenuResult show_book_tables_page(Table *table);
// (no local string-list helpers required here)
static int prompt_add_column_at_internal(Table *table, int col_index, int focus_inserted, const char *title);
static int prompt_add_row_at_internal(Table *table, int row_index, int focus_inserted, const char *title);

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

static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i) free(list[i]);
    free(list);
}

void prompt_add_column(Table *table) {
    (void)prompt_add_column_at_internal(table, table ? table->column_count : 0, 0, "Add Column");
}

int prompt_insert_column_at(Table *table, int col_index)
{
    const char *title = "Add Column";

    if (table && table->column_count > 0) {
        title = (col_index <= cursor_col) ? "Add Column Left" : "Add Column Right";
    }
    return prompt_add_column_at_internal(table, col_index, 1, title);
}

static int prompt_add_column_at_internal(Table *table, int col_index, int focus_inserted, const char *title)
{
    char name[MAX_INPUT];
    int name_len = show_text_input_modal(title ? title : "Add Column",
                                     "[Enter] Next   [Esc] Cancel",
                                     "Name:",
                                     name,
                                     sizeof(name),
                                     false);
    if (name_len < 0) {
        return -1;
    }

    const char *type_items[] = { "int", "float", "str", "bool" };
    int selected = ui_dialog_show_simple_list_modal("[2/2] Select column type", type_items, 4, 0);
    if (selected < 0) return -1;

    DataType type = TYPE_UNKNOWN;
    if (selected == 0) type = TYPE_INT;
    else if (selected == 1) type = TYPE_FLOAT;
    else if (selected == 2) type = TYPE_STR;
    else if (selected == 3) type = TYPE_BOOL;
    if (type != TYPE_UNKNOWN) {
        char err[256] = {0};
        UiHistoryApplyResult result = {0};
        if (ui_history_insert_column(table, col_index, name, type, &result, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to add column.");
        } else {
            if (!focus_inserted) {
                result.focus_header = (cursor_row < 0);
                result.focus_actual_row = (cursor_row >= 0) ? ui_actual_row_for_visible(table, cursor_row) : -1;
                result.focus_col = cursor_col;
            }
            if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                show_error_message(err);
            }
            return 0;
        }
    }
    return -1;
}

void prompt_add_row(Table *table) {
    (void)prompt_add_row_at_internal(table, table ? table->row_count : 0, 0, "Add Row");
}

int prompt_insert_row_at(Table *table, int row_index)
{
    const char *title = "Add Row";

    if (cursor_row == -1 || row_index <= 0) title = "Add Row Above";
    else title = "Add Row Below";
    return prompt_add_row_at_internal(table, row_index, 1, title);
}

static int prompt_add_row_at_internal(Table *table, int row_index, int focus_inserted, const char *title)
{
    if (!table || table->column_count == 0) {
        show_error_message("Add at least one column first.");
        return -1;
    }

    char **input_strings = calloc(table->column_count, sizeof(char *));
    if (!input_strings) {
        show_error_message("Out of memory");
        return -1;
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
            int rc = show_text_input_modal(title ? title : "Add Row",
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
        UiHistoryApplyResult result = {0};
        if (ui_history_insert_row(table, row_index, (const char **)input_strings, &result, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to add row.");
        } else {
            if (!focus_inserted) {
                result.focus_header = (cursor_row < 0);
                result.focus_actual_row = (cursor_row >= 0) ? ui_actual_row_for_visible(table, cursor_row) : row_index;
                result.focus_col = cursor_col;
            }
            if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                show_error_message(err);
            }
            for (int i = 0; i < table->column_count; ++i) {
                free(input_strings[i]);
            }
            free(input_strings);
            return 0;
        }
    }

    for (int i = 0; i < table->column_count; ++i) {
        free(input_strings[i]);
    }
    free(input_strings);
    return -1;
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
                pick = ui_dialog_show_simple_list_modal(title, opts, 2, 0);
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

        selected_col = ui_dialog_show_simple_list_modal("Sort Rows By Column", labels, table->column_count, selected_col);
        if (selected_col < 0) break;

        selected_order = ui_dialog_show_simple_list_modal("Sort Order", order_items, 2, selected_order);
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

        selected_col = ui_dialog_show_simple_list_modal("Filter Rows By Column", labels, table->column_count, selected_col);
        if (selected_col < 0) break;

        while (1) {
            char err[256] = {0};

            selected_op = ui_dialog_show_simple_list_modal("Filter Operator", op_items, 6, selected_op);
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
