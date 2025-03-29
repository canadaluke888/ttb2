#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include "../include/tablecraft.h"
#include "../include/errors.h"

#define MAX_INPUT 128

static int editing_mode = 0;
static int cursor_row = -1;
static int cursor_col = 0;

void draw_ui(Table *table);
void draw_table_grid(Table *t);
void prompt_add_column(Table *table);
void prompt_add_row(Table *table);

void init_colors() {
    start_color();
    use_default_colors();  // Transparent background support

    init_pair(1, COLOR_GREEN, -1);    // Title
    init_pair(2, COLOR_WHITE, -1); // Table body
    init_pair(3, COLOR_CYAN, -1); // Input box labels
    init_pair(4, COLOR_YELLOW, -1); // Input prompts
    init_pair(5, COLOR_MAGENTA, -1);  // Footer / hotkeys
    init_pair(6, COLOR_BLUE, -1); // Unicode borders
    init_pair(10, COLOR_RED, -1);
    init_pair(11, COLOR_GREEN, -1);
    init_pair(12, COLOR_YELLOW, -1);    
    init_pair(13, COLOR_BLUE, -1);
    init_pair(14, COLOR_MAGENTA, -1);
    init_pair(15, COLOR_CYAN, -1);
    init_pair(16, COLOR_WHITE, -1);
}

bool validate_input(const char *input, DataType type) {
    if (!input || strlen(input) == 0) return false;

    char *endptr;

    switch (type) {
        case TYPE_INT:
            strtol(input, &endptr, 10);
            return *endptr == '\0';
        case TYPE_FLOAT:
            strtof(input, &endptr);
            return *endptr == '\0';
        case TYPE_BOOL:
            return (
                strcasecmp(input, "true") == 0 ||
                strcasecmp(input, "false") == 0 ||
                strcmp(input, "1") == 0 ||
                strcmp(input, "0") == 0
            );
        case TYPE_STR:
            return true;
        default:
            return false;
    }
}

void edit_header_cell(Table *t, int col) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++) mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, " Rename column \"%s\":", t->columns[col].name);
    attroff(COLOR_PAIR(3) | A_BOLD);
    mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 2, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(4));
    mvprintw(input_box_y + 2, input_box_x + 1, " > ");
    attroff(COLOR_PAIR(4));
    mvaddch(input_box_y + 2, input_box_x + input_box_width - 1, ACS_VLINE);

    mvaddch(input_box_y + 3, input_box_x, ACS_LLCORNER);
    for (int i = 1; i < input_box_width - 1; i++) mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y + 3, input_box_x + input_box_width - 1, ACS_LRCORNER);

    move(input_box_y + 2, input_box_x + 4);
    getnstr(name, MAX_INPUT - 1);

    if (strlen(name) > 0) {
        free(t->columns[col].name);
        t->columns[col].name = strdup(name);
    }

    noecho();
    curs_set(0);
}

void edit_body_cell(Table *t, int row, int col) {
    echo();
    curs_set(1);

    char value[MAX_INPUT];
    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    const char *col_name = t->columns[col].name;
    const char *type = type_to_string(t->columns[col].type);

    while (1) {
        for (int line = 0; line < 6; line++) {
            move(input_box_y + line, 0);
            clrtoeol();
        }

        // Top border
        mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
        for (int i = 1; i < input_box_width - 1; i++) mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
        mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

        // Prompt line
        mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(input_box_y + 1, input_box_x + 1, " Edit value for \"%s (%s)\"", col_name, type);
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
        for (int i = 1; i < input_box_width - 1; i++) mvaddch(input_box_y + 3, input_box_x + i, ACS_HLINE);
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
            case TYPE_UNKNOWN:
            default: {
                ptr = NULL;
                break;
            }
        }

        if (t->rows[row].values[col]) free(t->rows[row].values[col]);
        t->rows[row].values[col] = ptr;
        break; // Valid input complete
    }

    noecho();
    curs_set(0);
}


void start_ui_loop(Table *table) {
    keypad(stdscr, TRUE);  // Needed for arrow keys
    int ch;

    while (1) {
        draw_ui(table);
        ch = getch();

        if (!editing_mode) {
            if (ch == 'q' || ch == 'Q') break;
            else if (ch == 'c' || ch == 'C') prompt_add_column(table);
            else if (ch == 'r' || ch == 'R') {
                if (table->column_count == 0) {
                    show_error_message("You must add at least one column before adding rows.");
                } else {
                    prompt_add_row(table);
                }
            } else if (ch == 'e' || ch == 'E') {
                editing_mode = 1;
                cursor_row = -1;  // Header
                cursor_col = 0;
            }
        } else {
            switch (ch) {
                case KEY_LEFT:
                    if (cursor_col > 0) cursor_col--;
                    break;
                case KEY_RIGHT:
                    if (cursor_col < table->column_count - 1) cursor_col++;
                    break;
                case KEY_UP:
                    if (cursor_row > -1) cursor_row--;
                    break;
                case KEY_DOWN:
                    if (cursor_row < table->row_count - 1) cursor_row++;
                    break;
                case 27: // ESC
                    editing_mode = 0;
                    break;
                case '\n': // ENTER
                    if (cursor_row == -1) {
                        edit_header_cell(table, cursor_col);
                    } else {
                        edit_body_cell(table, cursor_row, cursor_col);
                    }
                    break;
            }
        }
    }
}

void draw_ui(Table *table) {
    clear();

    int title_x = (COLS - strlen(table->name)) / 2;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, title_x, "%s", table->name);
    attroff(COLOR_PAIR(1) | A_BOLD);

    draw_table_grid(table);

    attron(COLOR_PAIR(5));
    if (!editing_mode) {
        mvprintw(LINES - 2, 2, "[C] Add Column    [R] Add Row    [E] Edit Mode    [Q] Quit");
    } else {
        mvprintw(LINES - 2, 2, "[←][→][↑][↓] Navigate    [Enter] Edit Cell    [Esc] Exit Edit Mode");
    }
    attroff(COLOR_PAIR(5));

    refresh();
}

void prompt_add_column(Table *table) {
    echo();
    curs_set(1);

    char name[MAX_INPUT];
    char type_str[MAX_INPUT];

    int input_box_width = COLS - 4;
    int input_box_x = 2;
    int input_box_y = LINES / 2 - 2;

    // ===============================
    // Step 1: Prompt for Column Name
    // ===============================
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, "[1/2] Enter column name");
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
    getnstr(name, MAX_INPUT - 1);

    // ===============================
    // Step 2: Prompt for Data Type
    // ===============================
    for (int line = 0; line < 5; line++) {
        move(input_box_y + line, 0);
        clrtoeol();
    }

    mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
    for (int i = 1; i < input_box_width - 1; i++)
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

    mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(input_box_y + 1, input_box_x + 1, "[2/2] Enter type for \"%s\" (int, float, str, bool)", name);
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
    getnstr(type_str, MAX_INPUT - 1);

    // ===============================
    // Add Column
    // ===============================
    DataType type = parse_type_from_string(type_str);
    if (type != TYPE_UNKNOWN) {
        add_column(table, name, type);
    }

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
            // Clear input box area
            for (int line = 0; line < 6; line++) {
                move(input_box_y + line, 0);
                clrtoeol();
            }

            // Top border
            mvaddch(input_box_y, input_box_x, ACS_ULCORNER);
            for (int i = 1; i < input_box_width - 1; i++)
                mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
            mvaddch(input_box_y, input_box_x + input_box_width - 1, ACS_URCORNER);

            // Row 1 (prompt)
            mvaddch(input_box_y + 1, input_box_x, ACS_VLINE);
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(input_box_y + 1, input_box_x + 1, " [%d/%d] Enter value for \"%s (%s)\"",
                     i + 1, table->column_count, col_name, col_type);
            attroff(COLOR_PAIR(3) | A_BOLD);
            mvaddch(input_box_y + 1, input_box_x + input_box_width - 1, ACS_VLINE);

            // Row 2 (input)
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

            // Input position
            move(input_box_y + 2, input_box_x + 4);
            getnstr(input_strings[i], MAX_INPUT - 1);

            if (validate_input(input_strings[i], table->columns[i].type)) {
                break;
            } else {
                show_error_message("Invalid input.");
            }
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


void draw_table_grid(Table *t) {
    if (t->column_count == 0) return;

    int x = 2, y = 2;
    int *col_widths = malloc(t->column_count * sizeof(int));

    for (int j = 0; j < t->column_count; j++) {
        char header_buf[128];
        snprintf(header_buf, sizeof(header_buf), "%s (%s)",
                 t->columns[j].name, type_to_string(t->columns[j].type));
        int max = strlen(header_buf) + 2;

        for (int i = 0; i < t->row_count; i++) {
            char buf[64];
            if (!t->rows[i].values[j]) continue;
            if (t->columns[j].type == TYPE_INT)
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_FLOAT)
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_BOOL)
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[i].values[j]) ? "true" : "false");
            else
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[i].values[j]);

            int len = strlen(buf) + 2;
            if (len > max) max = len;
        }

        col_widths[j] = max;
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┏");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("━");
        addstr((j < t->column_count - 1) ? "┳" : "┓");
    }
    attroff(COLOR_PAIR(6));

    move(y++, x);
    attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    for (int j = 0; j < t->column_count; j++) {
        const char *name = t->columns[j].name;
        const char *type = type_to_string(t->columns[j].type);

        if (editing_mode && cursor_row == -1 && cursor_col == j)
            attron(A_REVERSE);

        attron(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);
        printw(" %s", name);
        attroff(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);

        attron(COLOR_PAIR(3));
        printw(" (%s)", type);
        attroff(COLOR_PAIR(3));

        if (editing_mode && cursor_row == -1 && cursor_col == j)
            attroff(A_REVERSE);

        int used = strlen(name) + strlen(type) + 4;
        for (int s = used; s < col_widths[j]; s++) addch(' ');

        attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┡");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("━");
        addstr((j < t->column_count - 1) ? "╇" : "┩");
    }
    attroff(COLOR_PAIR(6));

    for (int i = 0; i < t->row_count; i++) {
        move(y++, x);
        attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        for (int j = 0; j < t->column_count; j++) {
            char buf[64] = "";
            if (t->columns[j].type == TYPE_INT && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_FLOAT && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[i].values[j]);
            else if (t->columns[j].type == TYPE_BOOL && t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[i].values[j]) ? "true" : "false");
            else if (t->rows[i].values[j])
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[i].values[j]);

            if (editing_mode && cursor_row == i && cursor_col == j)
                attron(A_REVERSE);

            attron(COLOR_PAIR(t->columns[j].color_pair_id));
            printw(" %s", buf);
            int used = strlen(buf) + 1;
            for (int s = used; s < col_widths[j]; s++) addch(' ');
            attroff(COLOR_PAIR(t->columns[j].color_pair_id));

            if (editing_mode && cursor_row == i && cursor_col == j)
                attroff(A_REVERSE);

            attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        }

        if (i < t->row_count - 1) {
            move(y++, x);
            attron(COLOR_PAIR(6));
            addstr("├");
            for (int j = 0; j < t->column_count; j++) {
                for (int k = 0; k < col_widths[j]; k++) addstr("─");
                addstr((j < t->column_count - 1) ? "┼" : "┤");
            }
            attroff(COLOR_PAIR(6));
        }
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "└");
    for (int j = 0; j < t->column_count; j++) {
        for (int i = 0; i < col_widths[j]; i++) addstr("─");
        addstr((j < t->column_count - 1) ? "┴" : "┘");
    }
    attroff(COLOR_PAIR(6));

    free(col_widths);
}
