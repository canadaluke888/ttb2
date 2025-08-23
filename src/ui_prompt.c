#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "python_bridge.h"
#include "errors.h"
#include "ui/panel_manager.h"

#define MAX_INPUT 128

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

    // Step 2: column type
    werase(modal->win);
    box(modal->win, 0, 0);
    wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(modal->win, 1, 2, "[2/2] Enter type for \"%s\" (int, float, str, bool)", name);
    wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
    wattron(modal->win, COLOR_PAIR(4));
    mvwprintw(modal->win, 2, 2, " > ");
    wattroff(modal->win, COLOR_PAIR(4));
    pm_wnoutrefresh(shadow);
    pm_wnoutrefresh(modal);
    pm_update();
    mvwgetnstr(modal->win, 2, 5, type_str, MAX_INPUT - 1);

    DataType type = parse_type_from_string(type_str);
    if (type != TYPE_UNKNOWN)
        add_column(table, name, type);

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
    }

    noecho();
    curs_set(0);
}

void show_table_menu(Table *table) {
    echo();
    curs_set(1);

    int box_width = 30;
    int box_height = 7;
    int box_x = (COLS - box_width) / 2;
    int box_y = (LINES - box_height) / 2;

    // Clear area
    for (int i = 0; i < box_height; i++) {
        move(box_y + i, 0);
        clrtoeol();
    }

    // Border
    mvaddch(box_y, box_x, ACS_ULCORNER);
    for (int i = 1; i < box_width - 1; i++) mvaddch(box_y, box_x + i, ACS_HLINE);
    mvaddch(box_y, box_x + box_width - 1, ACS_URCORNER);

    for (int i = 1; i < box_height - 1; i++) {
        mvaddch(box_y + i, box_x, ACS_VLINE);
        mvaddch(box_y + i, box_x + box_width - 1, ACS_VLINE);
    }

    mvaddch(box_y + box_height - 1, box_x, ACS_LLCORNER);
    for (int i = 1; i < box_width - 1; i++) mvaddch(box_y + box_height - 1, box_x + i, ACS_HLINE);
    mvaddch(box_y + box_height - 1, box_x + box_width - 1, ACS_LRCORNER);

    // Menu text
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(box_y + 1, box_x + 2, "[1] Rename Table");
    mvprintw(box_y + 2, box_x + 2, "[2] Save Table");
    mvprintw(box_y + 3, box_x + 2, "[3] Load Table");
    mvprintw(box_y + 4, box_x + 2, "[Q] Cancel");
    attroff(COLOR_PAIR(3) | A_BOLD);
    refresh();

    int choice = getch();
    if (choice == '1') {
        prompt_rename_table(table);
    }
    else if (choice == '2') {
        show_save_format_menu(table);
    }

    // Clear menu after
    for (int i = 0; i < box_height + 1; i++) {
        move(box_y + i, 0);
        clrtoeol();
    }

    noecho();
    curs_set(0);

}

void show_save_format_menu(Table *table) {
    if (!is_python_available()) {
        show_error_message("Python 3 not found; export disabled.");
        return;
    }
    clear();
    refresh();

    echo();
    curs_set(1);

    int box_width = 30;
    int box_height = 9;
    int box_x = (COLS - box_width) / 2;
    int box_y = (LINES - box_height) / 2;

    // Draw menu border
    mvaddch(box_y, box_x, ACS_ULCORNER);
    for (int i = 1; i < box_width - 1; i++) mvaddch(box_y, box_x + i, ACS_HLINE);
    mvaddch(box_y, box_x + box_width - 1, ACS_URCORNER);

    for (int i = 1; i < box_height - 1; i++) {
        mvaddch(box_y + i, box_x, ACS_VLINE);
        mvaddch(box_y + i, box_x + box_width - 1, ACS_VLINE);
    }

    mvaddch(box_y + box_height - 1, box_x, ACS_LLCORNER);
    for (int i = 1; i < box_width - 1; i++) mvaddch(box_y + box_height - 1, box_x + i, ACS_HLINE);
    mvaddch(box_y + box_height - 1, box_x + box_width - 1, ACS_LRCORNER);

    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(box_y + 1, box_x + 2, "Select format to save:");
    mvprintw(box_y + 2, box_x + 2, "[1] PDF");
    mvprintw(box_y + 3, box_x + 2, "[2] XLSX");
    mvprintw(box_y + 4, box_x + 2, "[Q] Cancel");
    attroff(COLOR_PAIR(3) | A_BOLD);
    refresh();

    int choice = getch();
    const char *format = NULL;

    switch (choice) {
        case '1': format = "pdf"; break;
        case '2': format = "xlsx"; break;
        case 'q':
        case 'Q':
            return;
        default:
            show_error_message("Invalid selection.");
            return;
    }

    // Prompt for filename
    char filename[128];
    int prompt_y = box_y + box_height + 1;
    int prompt_width = 50;

    for (int line = 0; line < 5; line++) {
        move(prompt_y + line, 0);
        clrtoeol();
    }

    mvaddch(prompt_y, box_x, ACS_ULCORNER);
    for (int i = 1; i < prompt_width - 1; i++)
        mvaddch(prompt_y, box_x + i, ACS_HLINE);
    mvaddch(prompt_y, box_x + prompt_width - 1, ACS_URCORNER);

    mvaddch(prompt_y + 1, box_x, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(prompt_y + 1, box_x + 2, "Enter filename (no ext): ");
    attroff(COLOR_PAIR(4));
    mvaddch(prompt_y + 1, box_x + prompt_width - 1, ACS_VLINE);

    mvaddch(prompt_y + 2, box_x, ACS_VLINE);
    mvaddch(prompt_y + 2, box_x + prompt_width - 1, ACS_VLINE);

    mvaddch(prompt_y + 3, box_x, ACS_LLCORNER);
    for (int i = 1; i < prompt_width - 1; i++)
        mvaddch(prompt_y + 3, box_x + i, ACS_HLINE);
    mvaddch(prompt_y + 3, box_x + prompt_width - 1, ACS_LRCORNER);

    move(prompt_y + 2, box_x + 2);
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


