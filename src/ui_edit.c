#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "ui/panel_manager.h"

#define MAX_INPUT 128

// Allow editing header cell: rename or change type with validation warning
void edit_header_cell(Table *t, int col) {
    int selected = 0; // 0 = rename, 1 = change type, -1 = cancel
    int ch;
    int h = 5;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Edit column '%s':", t->columns[col].name);
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        // Options
        if (selected == 0) wattron(modal->win, A_REVERSE);
        mvwprintw(modal->win, 2, 2, "Rename Name");
        if (selected == 0) wattroff(modal->win, A_REVERSE);
        if (selected == 1) wattron(modal->win, A_REVERSE);
        mvwprintw(modal->win, 2, 20, "Edit Type");
        if (selected == 1) wattroff(modal->win, A_REVERSE);
        mvwprintw(modal->win, 3, 2, "Use arrow keys and Enter to select (Esc to cancel)");
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
        echo();
        curs_set(1);
        char name[MAX_INPUT];
        int box_w = COLS - 4;
        int bx = 2;
        int by = LINES / 2 - 2;
        for (int i = 0; i < 5; i++) { move(by + i, 0); clrtoeol(); }
        mvaddch(by, bx, ACS_ULCORNER);
        for (int i = 1; i < box_w - 1; i++) mvaddch(by, bx + i, ACS_HLINE);
        mvaddch(by, bx + box_w - 1, ACS_URCORNER);
        mvaddch(by + 1, bx, ACS_VLINE);
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(by + 1, bx + 1, " Rename column '%s':", t->columns[col].name);
        attroff(COLOR_PAIR(3) | A_BOLD);
        mvaddch(by + 1, bx + box_w - 1, ACS_VLINE);
        mvaddch(by + 2, bx, ACS_VLINE);
        attron(COLOR_PAIR(4)); mvprintw(by + 2, bx + 1, " > "); attroff(COLOR_PAIR(4));
        mvaddch(by + 2, bx + box_w - 1, ACS_VLINE);
        mvaddch(by + 3, bx, ACS_LLCORNER);
        for (int i = 1; i < box_w - 1; i++) mvaddch(by + 3, bx + i, ACS_HLINE);
        mvaddch(by + 3, bx + box_w - 1, ACS_LRCORNER);
        move(by + 2, bx + 4);
        getnstr(name, MAX_INPUT - 1);
        if (strlen(name) > 0) {
            free(t->columns[col].name);
            t->columns[col].name = strdup(name);
        }
        noecho();
        curs_set(0);
    } else {
        // Change column type
        echo();
        curs_set(1);
        char type_str[MAX_INPUT];
        int box_w = COLS - 4;
        int bx = 2;
        int by = LINES / 2 - 2;
        PmNode *sh2 = pm_add(by + 1, bx + 2, 4, box_w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *mo2 = pm_add(by, bx, 4, box_w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        werase(mo2->win);
        box(mo2->win, 0, 0);
        wattron(mo2->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(mo2->win, 1, 2, "New type for '%s' (int, float, str, bool):", t->columns[col].name);
        wattroff(mo2->win, COLOR_PAIR(3) | A_BOLD);
        wattron(mo2->win, COLOR_PAIR(4)); mvwprintw(mo2->win, 2, 2, " > "); wattroff(mo2->win, COLOR_PAIR(4));
        pm_wnoutrefresh(sh2); pm_wnoutrefresh(mo2); pm_update();
        mvwgetnstr(mo2->win, 2, 5, type_str, MAX_INPUT - 1);
        DataType new_type = parse_type_from_string(type_str);
        if (new_type == TYPE_UNKNOWN) {
            show_error_message("Unknown type, no change applied.");
        } else {
            t->columns[col].type = new_type;
            // Validate existing data
            int conflicts = 0;
            for (int r = 0; r < t->row_count; r++) {
                char buf[MAX_INPUT];
                void *v = t->rows[r].values[col];
                switch (new_type) {
                    case TYPE_INT: snprintf(buf, sizeof(buf), "%d", *(int *)v); break;
                    case TYPE_FLOAT: snprintf(buf, sizeof(buf), "%f", *(float *)v); break;
                    case TYPE_BOOL: snprintf(buf, sizeof(buf), "%s", (*(int *)v) ? "true" : "false"); break;
                    case TYPE_STR: snprintf(buf, sizeof(buf), "%s", (char *)v); break;
                    default: buf[0] = '\0';
                }
                if (!validate_input(buf, new_type)) conflicts++;
            }
            pm_remove(mo2); pm_remove(sh2); pm_update();
            if (conflicts > 0) {
                char warn[MAX_INPUT];
                snprintf(warn, sizeof(warn), "%d cell(s) conflict with new type.", conflicts);
                show_error_message(warn);
            }
        }
        noecho();
        curs_set(0);
    }
}

void edit_body_cell(Table *t, int row, int col) {
    echo();
    curs_set(1);

    char value[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;
    const char *col_name = t->columns[col].name;
    const char *type_str = type_to_string(t->columns[col].type);

    while (1) {
        for (int line = 0; line < 6; line++) {
            move(input_box_y + line, 0);
            clrtoeol();
        }

        // Top border
        mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

        // Prompt line
        mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(input_box_y + 1, input_box_x + 1, " Edit value for \"%s (%s)\"", col_name, type_str);
        attroff(COLOR_PAIR(3) | A_BOLD);
        mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

        // Input line
        mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
        attron(COLOR_PAIR(4));
        mvprintw(input_box_y + 2, input_box_x + 1, " > ");
        attroff(COLOR_PAIR(4));
        mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

        // Bottom border
        mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
        for (int i = 1; i < input_box_width - 1; i++)
            mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

        move(input_box_y + 2, input_box_x + 4);
        getnstr(value, MAX_INPUT - 1);

        if (!validate_input(value, t->columns[col].type)) {
            show_error_message("Invalid input.");
            continue;
        }

        void *ptr = NULL;
        switch (t->columns[col].type) {
            case TYPE_INT: {
                int *i = malloc(sizeof(int));
                *i = atoi(value);
                ptr = i;
                break;
            }
            case TYPE_FLOAT: {
                float *f = malloc(sizeof(float));
                *f = atof(value);
                ptr = f;
                break;
            }
            case TYPE_BOOL: {
                int *b = malloc(sizeof(int));
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
        break; // Valid input complete
    }

    noecho();
    curs_set(0);
}
