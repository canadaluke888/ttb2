#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "table.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"
#include "db_manager.h"
#include "table_ops.h"
#include "ui_history.h"

#define MAX_INPUT 128

// Allow editing header cell: rename or change type with validation warning
void edit_header_cell(Table *t, int col) {
    int selected = 0; /* 0=rename,1=change type */
    int ch;
    int h = 4;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w,
                             PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w,
                           PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Edit column '%s':", t->columns[col].name);
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        // Options
        if (selected == 0) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
        mvwprintw(modal->win, 2, 2, "Rename Column");
        if (selected == 0) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        if (selected == 1) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
        mvwprintw(modal->win, 2, 20, "Change Type");
        if (selected == 1) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        pm_wnoutrefresh(shadow);
        pm_wnoutrefresh(modal);
        pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_LEFT || ch == KEY_UP) {
            selected = 0;
        } else if (ch == KEY_RIGHT || ch == KEY_DOWN) {
            selected = 1;
        } else if (ch == '\n') {
            break;
        } else if (ch == 27) {
            selected = -1;
            break;
        }
    }
    pm_remove(modal);
    pm_remove(shadow);
    pm_update();
    if (selected < 0) return;

    if (selected == 0) {
        // Rename column
        char label[160];
        snprintf(label, sizeof(label), "Rename column '%s':", t->columns[col].name);
        char name_buf[MAX_INPUT];
        int rc = show_text_input_modal("Rename Column",
                                       "[Enter] Save   [Esc] Cancel",
                                       label,
                                       name_buf,
                                       sizeof(name_buf),
                                       false);
        if (rc >= 0 && strlen(name_buf) > 0) {
            char err[256] = {0};
            UiHistoryApplyResult result = {0};
            if (ui_history_rename_column(t, col, name_buf, &result, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Rename failed.");
            } else {
                if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
                    show_error_message(err);
                }
            }
        }
    } else {
        // Change column type via list modal (no typing)
        noecho();
        curs_set(0);
        const char *type_items[] = { "int", "float", "str", "bool" };
        int selected_type = 0;
        int h2 = 7; int w2 = COLS - 4; int y2 = (LINES - h2) / 2; int x2 = 2;
        PmNode *sh2 = pm_add(y2 + 1, x2 + 2, h2, w2, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *mo2 = pm_add(y2, x2, h2, w2, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(mo2->win, TRUE);
        int ch2;
        while (1) {
            werase(mo2->win);
            box(mo2->win, 0, 0);
            wattron(mo2->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(mo2->win, 1, 2, "New type for '%s'", t->columns[col].name);
            wattroff(mo2->win, COLOR_PAIR(3) | A_BOLD);
            // Title underline
            mvwhline(mo2->win, 2, 1, ACS_HLINE, w2 - 2);
            mvwaddch(mo2->win, 2, 0, ACS_LTEE);
            mvwaddch(mo2->win, 2, w2 - 1, ACS_RTEE);
            for (int i = 0; i < 4; ++i) {
                if (i == selected_type) wattron(mo2->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(mo2->win, 3 + i, 2, "%s", type_items[i]);
                if (i == selected_type) wattroff(mo2->win, COLOR_PAIR(4) | A_BOLD);
            }
            pm_wnoutrefresh(sh2); pm_wnoutrefresh(mo2); pm_update();
            ch2 = wgetch(mo2->win);
            if (ch2 == KEY_UP) selected_type = (selected_type > 0) ? selected_type - 1 : 3;
            else if (ch2 == KEY_DOWN) selected_type = (selected_type + 1) % 4;
            else if (ch2 == '\n') break;
            else if (ch2 == 27) { selected_type = -1; break; }
        }
        DataType old_type = t->columns[col].type;
        DataType new_type = TYPE_UNKNOWN;
        if (selected_type == 0) new_type = TYPE_INT;
        else if (selected_type == 1) new_type = TYPE_FLOAT;
        else if (selected_type == 2) new_type = TYPE_STR;
        else if (selected_type == 3) new_type = TYPE_BOOL;
        if (new_type == TYPE_UNKNOWN) {
            /* close prompt panels before showing warning */
            pm_remove(mo2);
            pm_remove(sh2);
            pm_update();
            show_error_message("Unknown type, no change applied.");
        } else {
            /* validate convertibility of existing data to the new type */
            int conflicts = 0;
            for (int r = 0; r < t->row_count; r++) {
                void *v = (t->rows[r].values ? t->rows[r].values[col] : NULL);
                if (!v) continue; /* empty cell */
                char buf[128];
                switch (old_type) {
                    case TYPE_INT:
                        snprintf(buf, sizeof(buf), "%d", *(int *)v);
                        break;
                    case TYPE_FLOAT:
                        snprintf(buf, sizeof(buf), "%g", *(float *)v);
                        break;
                    case TYPE_BOOL:
                        snprintf(buf, sizeof(buf), "%s", (*(int *)v) ? "true" : "false");
                        break;
                    case TYPE_STR:
                    default:
                        snprintf(buf, sizeof(buf), "%s", (char *)v);
                        break;
                }
                if (!validate_input(buf, new_type))
                    conflicts++;
            }

            pm_remove(mo2);
            pm_remove(sh2);
            pm_update();

            if (conflicts > 0) {
                /* offer force option: convert where possible; clear incompatible */
                int h = 5;
                int w = COLS - 8;
                int y = (LINES - h) / 2;
                int x = 4;
                PmNode *sh3 = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
                PmNode *mo3 = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
                box(mo3->win, 0, 0);
                wattron(mo3->win, COLOR_PAIR(10) | A_BOLD);
                mvwprintw(mo3->win, 1, 2, "%d cell(s) conflict with new type.", conflicts);
                wattroff(mo3->win, COLOR_PAIR(10) | A_BOLD);
                wattron(mo3->win, COLOR_PAIR(11));
                mvwprintw(mo3->win, 2, 2, "Press F to force (incompatible cells cleared), or Esc to cancel");
                wattroff(mo3->win, COLOR_PAIR(11));
                pm_wnoutrefresh(sh3); pm_wnoutrefresh(mo3); pm_update();
                int key = wgetch(mo3->win);

                bool do_force = (key == 'f' || key == 'F');
                pm_remove(mo3); pm_remove(sh3); pm_update();

                if (do_force) {
                    char err[256] = {0};
                    UiHistoryApplyResult result = {0};
                    if (ui_history_change_column_type(t, col, new_type, &result, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Type change failed.");
                    } else {
                        if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
                            show_error_message(err);
                        }
                    }
                }
            } else {
                char err[256] = {0};
                UiHistoryApplyResult result = {0};
                if (ui_history_change_column_type(t, col, new_type, &result, err, sizeof(err)) != 0) {
                    show_error_message(err[0] ? err : "Type change failed.");
                } else {
                    if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
                        show_error_message(err);
                    }
                }
            }
        }
        noecho();
        curs_set(0);
    }
}

void edit_body_cell(Table *t, int row, int col) {
    if (!t) return;

    const char *col_name = t->columns[col].name;
    char value[MAX_INPUT] = {0};
    char prompt_label[256];

    while (1) {
        char current_value[MAX_INPUT] = {0};
        if (ui_format_cell_value(t, row, col, current_value, sizeof(current_value)) != 0) {
            current_value[0] = '\0';
        }
        snprintf(prompt_label, sizeof(prompt_label), "Edit '%s' in \"%s\":",
                 current_value[0] ? current_value : "",
                 col_name);

        int rc = show_text_input_modal("Edit Cell",
                                       "[Enter] Save   [Esc] Cancel",
                                       prompt_label,
                                       value,
                                       sizeof(value),
                                       false);
        if (rc < 0) {
            return;
        }

        if (value[0] == '\0') {
            char err[256] = {0};
            UiHistoryApplyResult result = {0};
            if (ui_history_clear_cell(t, row, col, &result, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to clear cell.");
                continue;
            }
            if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
                show_error_message(err);
            }
            break;
        }

        if (!validate_input(value, t->columns[col].type)) {
            show_error_message("Invalid input.");
            value[0] = '\0';
            continue;
        }

        {
            char err[256] = {0};
            UiHistoryApplyResult result = {0};
            if (ui_history_set_cell(t, row, col, value, &result, err, sizeof(err)) != 0) {
                show_error_message(err[0] ? err : "Failed to update cell.");
                value[0] = '\0';
                continue;
            }
            if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
                show_error_message(err);
            }
        }
        break;
    }
}

// Small list menu confirm (returns selected index or -1 on cancel)
static int list_confirm(const char *title, const char **items, int count) {
    int h = (count + 3); if (h < 7) h = 7;
    int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);
    int sel = 0, ch;
    while (1) {
        werase(modal->win); box(modal->win, 0, 0);
        if (title) { wattron(modal->win, COLOR_PAIR(3) | A_BOLD); mvwprintw(modal->win, 1, 2, "%s", title); wattroff(modal->win, COLOR_PAIR(3) | A_BOLD); }
        for (int i = 0; i < count; ++i) {
            if (i == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, 2 + i, 2, "%s", items[i]);
            if (i == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) sel = (sel > 0) ? sel - 1 : count - 1;
        else if (ch == KEY_DOWN) sel = (sel + 1) % count;
        else if (ch == '\n') break;
        else if (ch == 27) { sel = -1; break; }
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
    return sel;
}

int prompt_move_row_placement(Table *t, int source_row, int target_row)
{
    const char *opts[] = { "Above", "Below", "Cancel" };
    char title[96];

    (void)t;
    snprintf(title, sizeof(title), "Move Row %d relative to Row %d", source_row + 1, target_row + 1);
    return list_confirm(title, opts, 3);
}

int prompt_move_column_placement(Table *t, int source_col, int target_col)
{
    const char *opts[] = { "Left", "Right", "Cancel" };
    char title[160];

    snprintf(title,
             sizeof(title),
             "Move '%s' relative to '%s'",
             t->columns[source_col].name,
             t->columns[target_col].name);
    return list_confirm(title, opts, 3);
}

void confirm_delete_row_at(Table *t, int row) {
    if (!t || t->row_count <= 0 || row < 0 || row >= t->row_count) { show_error_message("No row to delete."); return; }
    const char *opts[] = { "Yes", "No" };
    char title[64]; snprintf(title, sizeof(title), "Delete Row %d?", row + 1);
    int pick = list_confirm(title, opts, 2);
    if (pick != 0) return;
    char err[256] = {0};
    UiHistoryApplyResult result = {0};
    if (ui_history_delete_row(t, row, &result, err, sizeof(err)) != 0) {
        show_error_message(err[0] ? err : "Failed to delete row.");
    } else {
        if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
            show_error_message(err);
        }
    }
}

void confirm_delete_column_at(Table *t, int col) {
    if (!t || t->column_count <= 0 || col < 0 || col >= t->column_count) { show_error_message("No column to delete."); return; }
    if (t->column_count == 1) { show_error_message("Cannot delete the last column."); return; }
    const char *opts[] = { "Yes", "No" };
    char title[96]; snprintf(title, sizeof(title), "Delete Column '%s'?", t->columns[col].name);
    int pick = list_confirm(title, opts, 2);
    if (pick != 0) return;
    char err[256] = {0};
    UiHistoryApplyResult result = {0};
    if (ui_history_delete_column(t, col, &result, err, sizeof(err)) != 0) {
        show_error_message(err[0] ? err : "Failed to delete column.");
    } else {
        if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
            show_error_message(err);
        }
    }
}

void prompt_clear_cell(Table *t, int row, int col) {
    if (!t || row < 0 || row >= t->row_count || col < 0 || col >= t->column_count) { show_error_message("No cell to clear."); return; }
    int h = 5; int w = COLS - 8; int y = (LINES - h) / 2; int x = 4;
    PmNode *sh = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *mo = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    box(mo->win, 0, 0);
    wattron(mo->win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(mo->win, 1, 2, "Clear cell R%dC%d? (Y/N)", row + 1, col + 1);
    wattroff(mo->win, COLOR_PAIR(3) | A_BOLD);
    pm_wnoutrefresh(sh); pm_wnoutrefresh(mo); pm_update();
    int key = wgetch(mo->win);
    pm_remove(mo); pm_remove(sh); pm_update();
    if (!(key == 'y' || key == 'Y' || key == '\n')) return;

    {
        char err[256] = {0};
        UiHistoryApplyResult result = {0};
        if (ui_history_clear_cell(t, row, col, &result, err, sizeof(err)) != 0) {
            show_error_message(err[0] ? err : "Failed to clear cell.");
            return;
        }
        if (ui_history_refresh(t, &result, err, sizeof(err)) != 0 && err[0]) {
            show_error_message(err);
        }
    }
}
