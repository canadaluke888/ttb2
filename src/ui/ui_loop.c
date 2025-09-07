#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"

// Define global UI state variables
int editing_mode = 0;
int cursor_row = -1;
int cursor_col = 0;
int search_mode = 0;
int search_hit_count = 0;
int search_hit_index = 0;
int col_page = 0;
int cols_visible = 0;
int total_pages = 1;
int col_start = 0;
int row_page = 0;
int rows_visible = 0;
int total_row_pages = 1;

// Local search state (only current in-memory table)
typedef struct { int row; int col; } SearchHit;
static SearchHit *hits = NULL;

static void clear_search_hits(void) {
    if (hits) { free(hits); hits = NULL; }
    search_hit_count = 0; search_hit_index = 0;
}

// Case-insensitive ASCII substring
static int ci_contains(const char *hay, const char *need) {
    if (!hay || !need) return 0;
    size_t nlen = strlen(need);
    if (nlen == 0) return 1;
    for (const char *p = hay; *p; ++p) {
        size_t i = 0;
        while (p[i] && i < nlen) {
            char a = p[i]; if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            char b = need[i]; if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static void gather_search_hits(Table *t, const char *query) {
    clear_search_hits();
    if (!t || t->column_count <= 0 || t->row_count <= 0) return;
    int cap = 0;
    for (int r = 0; r < t->row_count; ++r) {
        for (int c = 0; c < t->column_count; ++c) {
            char buf[128] = "";
            if (t->columns[c].type == TYPE_INT && t->rows[r].values[c])
                snprintf(buf, sizeof(buf), "%d", *(int *)t->rows[r].values[c]);
            else if (t->columns[c].type == TYPE_FLOAT && t->rows[r].values[c])
                snprintf(buf, sizeof(buf), "%.2f", *(float *)t->rows[r].values[c]);
            else if (t->columns[c].type == TYPE_BOOL && t->rows[r].values[c])
                snprintf(buf, sizeof(buf), "%s", (*(int *)t->rows[r].values[c]) ? "true" : "false");
            else if (t->rows[r].values[c])
                snprintf(buf, sizeof(buf), "%s", (char *)t->rows[r].values[c]);
            if (buf[0] == '\0') continue;
            if (ci_contains(buf, query)) {
                if (search_hit_count == cap) { cap = cap ? cap * 2 : 16; hits = realloc(hits, sizeof(SearchHit) * cap); }
                hits[search_hit_count].row = r; hits[search_hit_count].col = c; search_hit_count++;
            }
        }
    }
}

static int prompt_search_query(const char *title, const char *prompt, char *out, size_t out_sz) {
    echo(); curs_set(1);
    int w = COLS - 4; int x = 2; int y = LINES / 2 - 2;
    if (w < 30) w = COLS - 2;
    if (w < 10) w = 10;
    for (int i = 0; i < 5; ++i) { move(y + i, 0); clrtoeol(); }
    mvaddch(y, x, ACS_ULCORNER); for (int i = 1; i < w - 1; ++i) mvaddch(y, x + i, ACS_HLINE); mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + 1, x, ACS_VLINE);
    if (title) { attron(COLOR_PAIR(3) | A_BOLD); mvprintw(y + 1, x + 1, " %s", title); attroff(COLOR_PAIR(3) | A_BOLD); }
    mvaddch(y + 1, x + w - 1, ACS_VLINE);
    mvaddch(y + 2, x, ACS_VLINE); attron(COLOR_PAIR(4)); mvprintw(y + 2, x + 1, " %s", prompt); attroff(COLOR_PAIR(4)); mvaddch(y + 2, x + w - 1, ACS_VLINE);
    mvaddch(y + 3, x, ACS_LLCORNER); for (int i = 1; i < w - 1; ++i) mvaddch(y + 3, x + i, ACS_HLINE); mvaddch(y + 3, x + w - 1, ACS_LRCORNER);
    move(y + 2, x + 3 + (int)strlen(prompt));
    getnstr(out, (int)out_sz - 1);
    noecho(); curs_set(0);
    return (int)strlen(out);
}

static void enter_search(Table *table) {
    char query[128] = {0};
    if (prompt_search_query("Search", "Query: ", query, sizeof(query)) <= 0) return;
    gather_search_hits(table, query);
    if (search_hit_count <= 0) { show_error_message("No matches found."); return; }
    search_mode = 1;
    search_hit_index = 0;
    cursor_row = hits[0].row;
    cursor_col = hits[0].col;
}

static void exit_search(void) {
    search_mode = 0;
    clear_search_hits();
}

void start_ui_loop(Table *table) {
    keypad(stdscr, TRUE);  // Enable arrow keys
    int ch;

    while (1) {
        draw_ui(table);
        pm_update();
        ch = getch();

        if (ch == KEY_RESIZE) {
            pm_on_resize();
            continue;
        }

        if (search_mode) {
            // In search mode, arrow keys navigate matches, ESC exits
            if (ch == KEY_LEFT || ch == KEY_UP) {
                if (search_hit_count > 0) {
                    search_hit_index = (search_hit_index > 0) ? (search_hit_index - 1) : (search_hit_count - 1);
                    cursor_row = hits[search_hit_index].row;
                    cursor_col = hits[search_hit_index].col;
                }
            } else if (ch == KEY_RIGHT || ch == KEY_DOWN) {
                if (search_hit_count > 0) {
                    search_hit_index = (search_hit_index + 1) % (search_hit_count > 0 ? search_hit_count : 1);
                    cursor_row = hits[search_hit_index].row;
                    cursor_col = hits[search_hit_index].col;
                }
            } else if (ch == 27) { // ESC
                exit_search();
            }
        } else if (!editing_mode) {
            if (ch == 'q' || ch == 'Q')
                break;
            else if (ch == 'c' || ch == 'C')
                prompt_add_column(table);
            else if (ch == 'r' || ch == 'R') {
                if (table->column_count == 0)
                    show_error_message("You must add at least one column before adding rows.");
                else
                    prompt_add_row(table);
            }
            else if (ch == 'f' || ch == 'F') {
                enter_search(table);
            }
            else if (ch == 'e' || ch == 'E') {
                editing_mode = 1;
                cursor_row = -1;  // Set focus to header
                cursor_col = 0;
            }
            else if (ch == 'm' || ch == 'M') {
                show_table_menu(table);
            } else if (ch == KEY_LEFT) {
                if (col_page > 0) col_page--; // page index
            } else if (ch == KEY_RIGHT) {
                if (col_page < total_pages - 1) col_page++; // page index
            } else if (ch == KEY_UP) {
                if (row_page > 0) row_page--;
            } else if (ch == KEY_DOWN) {
                if (row_page < total_row_pages - 1) row_page++;
            }
        } else {
            switch (ch) {
                case KEY_LEFT:
                    // Do not page during edit; restrict to visible range
                    if (cursor_col > col_start) cursor_col--;
                    break;
                case KEY_RIGHT:
                    if (cursor_col < table->column_count - 1 &&
                        (cols_visible <= 0 || cursor_col < col_start + cols_visible - 1)) {
                        cursor_col++;
                    }
                    break;
                case KEY_UP:
                    // Header is always allowed; restrict data rows to visible page
                    if (cursor_row == -1) {
                        // stay on header
                    } else if (cursor_row > row_page * rows_visible) {
                        cursor_row--;
                    } else if (cursor_row > -1) {
                        cursor_row--; // move to header
                    }
                    break;
                case KEY_DOWN:
                    if (cursor_row < table->row_count - 1) {
                        int vis_end = row_page * rows_visible + rows_visible - 1;
                        if (rows_visible <= 0 || cursor_row < vis_end) {
                            cursor_row++;
                        }
                    }
                    break;
                case 27: // ESC key
                    editing_mode = 0;
                    break;
                case '\n': // Enter key
                    if (cursor_row == -1)
                        edit_header_cell(table, cursor_col);
                    else
                        edit_body_cell(table, cursor_row, cursor_col);
                    break;
            }
        }
    }
}
