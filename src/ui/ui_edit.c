#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"
#include "db_manager.h"

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
            free(t->columns[col].name);
            t->columns[col].name = strdup(name_buf);
            char err[256] = {0};
            db_autosave_table(t, err, sizeof(err));
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

                if (!do_force) {
                    /* keep old type and abort */
                    // no change; just return to UI
                } else {
                    /* perform conversion; clear incompatible cells */
                    for (int r = 0; r < t->row_count; r++) {
                        void *v = (t->rows[r].values ? t->rows[r].values[col] : NULL);
                        if (!v) {
                            /* set explicit default for new typed columns */
                            void *new_ptr = NULL;
                            switch (new_type) {
                                case TYPE_INT: { int *i = malloc(sizeof(int)); *i = 0; new_ptr = i; break; }
                                case TYPE_FLOAT: { float *f = malloc(sizeof(float)); *f = 0.0f; new_ptr = f; break; }
                                case TYPE_BOOL: { int *b = malloc(sizeof(int)); *b = 0; new_ptr = b; break; }
                                case TYPE_STR: new_ptr = strdup(""); break;
                                default: new_ptr = NULL; break;
                            }
                            t->rows[r].values[col] = new_ptr;
                            continue;
                        }

                        char buf[128];
                        switch (old_type) {
                            case TYPE_INT: snprintf(buf, sizeof(buf), "%d", *(int *)v); break;
                            case TYPE_FLOAT: snprintf(buf, sizeof(buf), "%g", *(float *)v); break;
                            case TYPE_BOOL: snprintf(buf, sizeof(buf), "%s", (*(int *)v) ? "true" : "false"); break;
                            case TYPE_STR: default: snprintf(buf, sizeof(buf), "%s", (char *)v); break;
                        }

                        void *new_ptr = NULL;
                        bool convertible = validate_input(buf, new_type);
                        if (convertible) {
                            switch (new_type) {
                                case TYPE_INT: { int *i = malloc(sizeof(int)); *i = atoi(buf); new_ptr = i; break; }
                                case TYPE_FLOAT: { float *f = malloc(sizeof(float)); *f = strtof(buf, NULL); new_ptr = f; break; }
                                case TYPE_BOOL: { int *b = malloc(sizeof(int)); *b = (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0); new_ptr = b; break; }
                                case TYPE_STR: new_ptr = strdup(buf); break;
                                default: new_ptr = NULL; break;
                            }
                        } else {
                            /* not convertible: clear to default */
                            switch (new_type) {
                                case TYPE_INT: { int *i = malloc(sizeof(int)); *i = 0; new_ptr = i; break; }
                                case TYPE_FLOAT: { float *f = malloc(sizeof(float)); *f = 0.0f; new_ptr = f; break; }
                                case TYPE_BOOL: { int *b = malloc(sizeof(int)); *b = 0; new_ptr = b; break; }
                                case TYPE_STR: new_ptr = strdup(""); break;
                                default: new_ptr = NULL; break;
                            }
                        }

                        free(t->rows[r].values[col]);
                        t->rows[r].values[col] = new_ptr;
                    }
                    t->columns[col].type = new_type;
                    {
                        char err[256] = {0};
                        db_autosave_table(t, err, sizeof(err));
                    }
                }
            } else {
                /* perform in-place conversion for existing data */
                for (int r = 0; r < t->row_count; r++) {
                    void *v = (t->rows[r].values ? t->rows[r].values[col] : NULL);
                    if (!v) continue;
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

                    void *new_ptr = NULL;
                    switch (new_type) {
                        case TYPE_INT: {
                            int *i = malloc(sizeof(int));
                            *i = atoi(buf);
                            new_ptr = i;
                            break;
                        }
                        case TYPE_FLOAT: {
                            float *f = malloc(sizeof(float));
                            *f = strtof(buf, NULL);
                            new_ptr = f;
                            break;
                        }
                        case TYPE_BOOL: {
                            int *b = malloc(sizeof(int));
                            *b = (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0);
                            new_ptr = b;
                            break;
                        }
                        case TYPE_STR: {
                            new_ptr = strdup(buf);
                            break;
                        }
                        default:
                            new_ptr = NULL;
                            break;
                    }

                    /* replace old value */
                    free(t->rows[r].values[col]);
                    t->rows[r].values[col] = new_ptr;
                }
                t->columns[col].type = new_type;
                {
                    char err[256] = {0};
                    db_autosave_table(t, err, sizeof(err));
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
    const char *type_str = type_to_string(t->columns[col].type);
    char prompt_label[160];
    snprintf(prompt_label, sizeof(prompt_label), "Edit value for '%s (%s)':", col_name, type_str);

    char value[MAX_INPUT] = {0};

    while (1) {
        int rc = show_text_input_modal("Edit Cell",
                                       "[Enter] Save   [Esc] Cancel",
                                       prompt_label,
                                       value,
                                       sizeof(value),
                                       false);
        if (rc < 0) {
            return;
        }

        if (!validate_input(value, t->columns[col].type)) {
            show_error_message("Invalid input.");
            value[0] = '\0';
            continue;
        }

        void *ptr = NULL;
        switch (t->columns[col].type) {
            case TYPE_INT: {
                int *i = malloc(sizeof(int));
                if (!i) return;
                *i = atoi(value);
                ptr = i;
                break;
            }
            case TYPE_FLOAT: {
                float *f = malloc(sizeof(float));
                if (!f) return;
                *f = atof(value);
                ptr = f;
                break;
            }
            case TYPE_BOOL: {
                int *b = malloc(sizeof(int));
                if (!b) return;
                *b = (strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0);
                ptr = b;
                break;
            }
            case TYPE_STR: {
                ptr = strdup(value);
                break;
            }
            default:
                ptr = NULL;
                break;
        }

        if (t->rows[row].values[col])
            free(t->rows[row].values[col]);
        t->rows[row].values[col] = ptr;
        {
            char err[256] = {0};
            db_autosave_table(t, err, sizeof(err));
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

// Internal helpers to mutate the table safely
static void delete_row_internal(Table *t, int r) {
    if (!t || r < 0 || r >= t->row_count) return;
    if (t->rows[r].values) {
        for (int c = 0; c < t->column_count; ++c) {
            if (t->rows[r].values[c]) free(t->rows[r].values[c]);
        }
        free(t->rows[r].values);
    }
    if (r < t->row_count - 1) {
        memmove(&t->rows[r], &t->rows[r+1], sizeof(Row) * (t->row_count - r - 1));
    }
    t->row_count--;
}

static void delete_column_internal(Table *t, int c) {
    if (!t || c < 0 || c >= t->column_count) return;
    if (t->column_count <= 1) return; // guard; caller should handle
    if (t->columns[c].name) free(t->columns[c].name);
    if (c < t->column_count - 1) {
        memmove(&t->columns[c], &t->columns[c+1], sizeof(Column) * (t->column_count - c - 1));
    }
    t->column_count--;
    // Shrink column array
    if (t->capacity_columns > t->column_count) {
        t->columns = realloc(t->columns, sizeof(Column) * t->column_count);
        t->capacity_columns = t->column_count;
    }
    // Adjust each row
    for (int r = 0; r < t->row_count; ++r) {
        if (!t->rows[r].values) continue;
        if (t->rows[r].values[c]) free(t->rows[r].values[c]);
        if (c < t->column_count) {
            memmove(&t->rows[r].values[c], &t->rows[r].values[c+1], sizeof(void*) * (t->column_count - c));
        }
        // shrink row values
        t->rows[r].values = realloc(t->rows[r].values, sizeof(void*) * t->column_count);
    }
}

void confirm_delete_row_at(Table *t, int row) {
    if (!t || t->row_count <= 0 || row < 0 || row >= t->row_count) { show_error_message("No row to delete."); return; }
    const char *opts[] = { "Yes", "No" };
    char title[64]; snprintf(title, sizeof(title), "Delete Row %d?", row + 1);
    int pick = list_confirm(title, opts, 2);
    if (pick != 0) return;
    delete_row_internal(t, row);
    char err[256] = {0}; db_autosave_table(t, err, sizeof(err));
}

void confirm_delete_column_at(Table *t, int col) {
    if (!t || t->column_count <= 0 || col < 0 || col >= t->column_count) { show_error_message("No column to delete."); return; }
    if (t->column_count == 1) { show_error_message("Cannot delete the last column."); return; }
    const char *opts[] = { "Yes", "No" };
    char title[96]; snprintf(title, sizeof(title), "Delete Column '%s'?", t->columns[col].name);
    int pick = list_confirm(title, opts, 2);
    if (pick != 0) return;
    delete_column_internal(t, col);
    char err[256] = {0}; db_autosave_table(t, err, sizeof(err));
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

    if (t->rows[row].values[col]) { free(t->rows[row].values[col]); t->rows[row].values[col] = NULL; }
    // Assign type-appropriate empty
    void *new_ptr = NULL;
    switch (t->columns[col].type) {
        case TYPE_INT: { int *i = malloc(sizeof(int)); *i = 0; new_ptr = i; break; }
        case TYPE_FLOAT: { float *f = malloc(sizeof(float)); *f = 0.0f; new_ptr = f; break; }
        case TYPE_BOOL: { int *b = malloc(sizeof(int)); *b = 0; new_ptr = b; break; }
        case TYPE_STR: default: new_ptr = strdup(""); break;
    }
    t->rows[row].values[col] = new_ptr;
    char err[256] = {0}; db_autosave_table(t, err, sizeof(err));
}
