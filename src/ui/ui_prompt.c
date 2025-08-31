#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "python_bridge.h"
#include "db_manager.h"
#include "errors.h"
#include "panel_manager.h"
#include "db_manager.h"

#define MAX_INPUT 128

static void draw_simple_list_modal(const char *title, const char **items, int count, int *io_selected);
static void free_string_list(char **list, int count); // local helper

static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i) free(list[i]);
    free(list);
}

static void draw_simple_list_modal(const char *title, const char **items, int count, int *io_selected) {
    int prev_vis = curs_set(0);
    noecho();
    int h = (count + 3); if (h < 7) h = 7;
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
        for (int i = 0; i < count; ++i) {
            if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, 2 + i, 2, "%s", items[i]);
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

void prompt_add_column(Table *table) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    char type_str[MAX_INPUT];
    int h = 4;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);

    // Step 1: column name
    werase(modal->win);
    box(modal->win, 0, 0);
    wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(modal->win, 1, 2, "[1/2] Enter column name");
    wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
    wattron(modal->win, COLOR_PAIR(4));
    mvwprintw(modal->win, 2, 2, " > ");
    wattroff(modal->win, COLOR_PAIR(4));
    pm_wnoutrefresh(shadow);
    pm_wnoutrefresh(modal);
    pm_update();
    mvwgetnstr(modal->win, 2, 5, name, MAX_INPUT - 1);

    // Step 2: column type via list menu
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

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();

    noecho();
    curs_set(0);
}

void prompt_add_row(Table *table) {
    echo();
    curs_set(1);

    char **input_strings = malloc(table->column_count * sizeof(char *));
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    for (int i = 0; i < table->column_count; i++) {
        input_strings[i] = malloc(MAX_INPUT);
        const char *col_name = table->columns[i].name;
        const char *col_type = type_to_string(table->columns[i].type);

        while (1) {
            for (int line = 0; line < 6; line++) {
                move(input_box_y + line, 0);
                clrtoeol();
            }
            mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
            for (int j = 0; j < input_box_width - 1; j++)
                mvaddch(input_box_y, input_box_x + j + 1, ACS_HLINE);
            mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);
            mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(input_box_y + 1, input_box_x + 1, " [%d/%d] Enter value for \"%s (%s)\"",
                     i + 1, table->column_count, col_name, col_type);
            attroff(COLOR_PAIR(3) | A_BOLD);
            mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);
            mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
            attron(COLOR_PAIR(4));
            mvprintw(input_box_y + 2, input_box_x + 1, " > ");
            attroff(COLOR_PAIR(4));
            mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);
            mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
            for (int j = 0; j < input_box_width - 1; j++)
                mvaddch(input_box_y + 3, input_box_x + j + 1, ACS_HLINE);
            mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);
            move(input_box_y + 2, input_box_x + 4);
            getnstr(input_strings[i], MAX_INPUT - 1);

            if (validate_input(input_strings[i], table->columns[i].type))
                break;
            else
                show_error_message("Invalid input.");
        }
    }

    add_row(table, (const char **)input_strings);
    {
        char err[256] = {0};
        db_autosave_table(table, err, sizeof(err));
    }

    for (int i = 0; i < table->column_count; i++) {
        free(input_strings[i]);
    }
    free(input_strings);

    noecho();
    curs_set(0);
}

void prompt_rename_table(Table *table) {
    echo();
    curs_set(1);

    char name[128];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;
    
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    // Draw border
    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, " Rename table \"%s\":", table->name);
    attroff(COLOR_PAIR(3) | A_BOLD);
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    attroff(COLOR_PAIR(4));
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

    move(input_box_y + 2, input_box_x + 4);
    getnstr(name, sizeof(name) - 1);

    if (strlen(name) > 0) {
        free(table->name);
        table->name = strdup(name);
        char err[256] = {0};
        db_autosave_table(table, err, sizeof(err));
    }

    noecho();
    curs_set(0);
}

void show_table_menu(Table *table) {
    /* Menu uses key navigation only: hide cursor */
    noecho();
    curs_set(0);

    int options_count = 7;
    int h = options_count + 3; /* title + options + padding */
    if (h < 7) h = 7; /* minimum height for comfort */
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    const char *labels[] = {"Rename", "Save", "Load", "New Table", "DB Manager", "Settings", "Cancel"};
    int selected = 0; /* 0=Rename,1=Save,2=Load,3=New,4=DB,5=Settings,6=Cancel */
    int ch;

    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Table Menu:");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);

        int yoff = 2;
        for (int i = 0; i < options_count; i++) {
            if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, yoff + i, 2, "%s", labels[i]);
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
        case 1: show_save_format_menu(table); break;
        case 2: {
            // Load table from current DB
            DbManager *cur = db_get_active();
            if (!cur || !db_is_connected(cur)) { show_error_message("No database connected."); break; }
            char err[256] = {0};
            char **tables = NULL; int tcount = 0;
            if (db_list_tables(cur, &tables, &tcount, err, sizeof(err)) != 0 || tcount == 0) {
                show_error_message("No tables to load.");
                free_string_list(tables, tcount);
                break;
            }
            const char **items = (const char**)tables; int pick = 0;
            // reuse DB modal helper
            extern void draw_list_modal(const char *, const char **, int, int *); // forward from ui_db.c
            draw_simple_list_modal("Select table to load", items, tcount, &pick);
            if (pick >= 0) {
                Table *loaded = db_load_table(cur, tables[pick], err, sizeof(err));
                if (!loaded) { show_error_message(err[0] ? err : "Load failed"); }
                else {
                    // Replace table content in place
                    // Free existing members
                    if (table->name) free(table->name);
                    for (int i = 0; i < table->column_count; i++) { if (table->columns[i].name) free(table->columns[i].name); }
                    free(table->columns);
                    for (int i = 0; i < table->row_count; i++) {
                        if (table->rows[i].values) {
                            for (int j = 0; j < table->column_count; j++) { if (table->rows[i].values[j]) free(table->rows[i].values[j]); }
                            free(table->rows[i].values);
                        }
                    }
                    free(table->rows);
                    // Move from loaded
                    table->name = loaded->name;
                    table->columns = loaded->columns;
                    table->column_count = loaded->column_count;
                    table->rows = loaded->rows;
                    table->row_count = loaded->row_count;
                    table->capacity_columns = loaded->capacity_columns;
                    table->capacity_rows = loaded->capacity_rows;
                    free(loaded);
                    // Save immediately (sync)
                    db_autosave_table(table, err, sizeof(err));
                }
            }
            free_string_list(tables, tcount);
            break;
        }
        case 3: {
            // New Table: ensure current table saved, then clear to start fresh
            DbManager *cur = db_get_active();
            if (cur && db_is_connected(cur)) {
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

void show_save_format_menu(Table *table) {
    if (!is_python_available()) {
        show_error_message("Python 3 not found; export disabled.");
        return;
    }

    /* Selection uses keys only: hide cursor */
    noecho();
    curs_set(0);

    /* Modal selection styled like header edit */
    const char *labels[] = {"PDF", "XLSX", "Cancel"};
    const char *values[] = {"pdf", "xlsx", NULL};
    int options_count = 3;
    int h = options_count + 3;
    if (h < 7) h = 7;
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
        mvwprintw(modal->win, 1, 2, "Select format to save:");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);

        for (int i = 0; i < options_count; i++) {
            if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, 2 + i, 2, "%s", labels[i]);
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

    const char *format = values[selected];

    pm_remove(modal);
    pm_remove(shadow);
    pm_update();
    if (!format) { /* Cancel */
        noecho();
        curs_set(0);
        return;
    }

    // Switch to visible cursor + echo for text input
    echo();
    curs_set(1);

    // Prompt for filename (consistent wide modal style)
    char filename[128];
    int box_w = COLS - 4;
    int bx = 2;
    int by = LINES / 2 - 2;

    for (int line = 0; line < 5; line++) { move(by + line, 0); clrtoeol(); }
    mvaddch(by, bx, ACS_ULCORNER);
    for (int i = 1; i < box_w - 1; i++) mvaddch(by, bx + i, ACS_HLINE);
    mvaddch(by, bx + box_w - 1, ACS_URCORNER);

    mvaddch(by + 1, bx, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(by + 1, bx + 2, "Enter filename (no ext): ");
    attroff(COLOR_PAIR(4));
    mvaddch(by + 1, bx + box_w - 1, ACS_VLINE);

    mvaddch(by + 2, bx, ACS_VLINE);
    mvaddch(by + 2, bx + box_w - 1, ACS_VLINE);

    mvaddch(by + 3, bx, ACS_LLCORNER);
    for (int i = 1; i < box_w - 1; i++) mvaddch(by + 3, bx + i, ACS_HLINE);
    mvaddch(by + 3, bx + box_w - 1, ACS_LRCORNER);

    move(by + 2, bx + 2);
    getnstr(filename, sizeof(filename) - 1);

    // Write temp CSV file
    FILE *f = fopen("tmp_export.csv", "w");
    if (!f) {
        show_error_message("Failed to write temp CSV.");
        return;
    }

    for (int j = 0; j < table->column_count; j++) {
        fprintf(f, "%s (%s)%s", table->columns[j].name, type_to_string(table->columns[j].type),
                (j < table->column_count - 1) ? "," : "\n");
    }

    for (int i = 0; i < table->row_count; i++) {
        for (int j = 0; j < table->column_count; j++) {
            void *v = table->rows[i].values[j];
            if (table->columns[j].type == TYPE_INT)
                fprintf(f, "%d", *(int *)v);
            else if (table->columns[j].type == TYPE_FLOAT)
                fprintf(f, "%.2f", *(float *)v);
            else if (table->columns[j].type == TYPE_BOOL)
                fprintf(f, "%s", (*(int *)v) ? "true" : "false");
            else
                fprintf(f, "%s", (char *)v);
            if (j < table->column_count - 1)
                fprintf(f, ",");
        }
        fprintf(f, "\n");
    }

    fclose(f);

    // Append file extension and call Python export
    char final_filename[256];
    snprintf(final_filename, sizeof(final_filename), "%s.%s", filename, format);
    call_python_export(format, final_filename);

    clear();
    refresh();
    noecho();
    curs_set(0);
}
