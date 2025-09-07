#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "tablecraft.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"

// Define global UI state variables
int editing_mode = 0;
int del_row_mode = 0;
int del_col_mode = 0;
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
int low_ram_mode = 0; // exported in ui.h
int row_gutter_enabled = 1; // exported in ui.h

// Local search state (current in-memory table/window)
typedef struct { int row; int col; int start; int len; } SearchHit;
static SearchHit *hits = NULL;
char search_query[128];
int search_sel_start = -1;
int search_sel_len = 0;

static void clear_search_hits(void) {
    if (hits) { free(hits); hits = NULL; }
    search_hit_count = 0; search_hit_index = 0;
}

static void trim_ascii(char *s) {
    if (!s) return;
    // trim leading
    int i = 0; while (s[i] && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    // trim trailing
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\n' || s[n-1] == '\r')) { s[n-1] = '\0'; n--; }
}

// Clamp cursor indices to current viewport (visible page boundaries)
static void clamp_cursor_viewport(const Table *table) {
    // Columns
    int cmin = col_start;
    int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table ? table->column_count - 1 : 0);
    if (cmax >= (table ? table->column_count : 0)) cmax = (table ? table->column_count - 1 : 0);
    if (cursor_col < cmin) cursor_col = cmin;
    if (cursor_col > cmax) cursor_col = (cmax >= 0 ? cmax : 0);
    // Rows (body only; header at -1 allowed outside)
    int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
    if (rmin < 0) rmin = 0;
    int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
    if (table && rmax >= table->row_count) rmax = table->row_count - 1;
    if (cursor_row >= 0) {
        if (cursor_row < rmin) cursor_row = rmin;
        if (cursor_row > rmax) cursor_row = rmax;
    }
}

// Case-insensitive ASCII substring
static int ci_find(const char *hay, const char *need) {
    if (!hay || !need) return -1;
    size_t nlen = strlen(need);
    if (nlen == 0) return 0;
    for (int pos = 0; hay[pos]; ++pos) {
        size_t i = 0;
        while (hay[pos + i] && i < nlen) {
            char a = hay[pos + i]; if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            char b = need[i];      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return pos;
    }
    return -1;
}

static void gather_search_hits(Table *t, const char *query) {
    clear_search_hits();
    if (!t || t->column_count <= 0 || t->row_count <= 0) return;
    strncpy(search_query, query, sizeof(search_query)-1); search_query[sizeof(search_query)-1] = '\0';
    trim_ascii(search_query);
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
            int start = ci_find(buf, query);
            if (start >= 0) {
                if (search_hit_count == cap) { cap = cap ? cap * 2 : 16; hits = realloc(hits, sizeof(SearchHit) * cap); }
                hits[search_hit_count].row = r; hits[search_hit_count].col = c;
                hits[search_hit_count].start = start; hits[search_hit_count].len = (int)strlen(query);
                search_hit_count++;
            }
        }
    }
}

static int prompt_search_query(const char *title, const char *prompt, char *out, size_t out_sz) {
    echo(); curs_set(1);
    nodelay(stdscr, FALSE); // block for text input
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
    nodelay(stdscr, TRUE); // restore non-blocking
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
    search_sel_start = hits[0].start;
    search_sel_len = hits[0].len;
    if (rows_visible > 0) row_page = cursor_row / rows_visible;
}

static void exit_search(void) {
    search_mode = 0;
    clear_search_hits();
}

void start_ui_loop(Table *table) {
    keypad(stdscr, TRUE);  // Enable arrow keys
    nodelay(stdscr, TRUE); // Non-blocking input to coalesce repeats
    int ch;

    while (1) {
        draw_ui(table);
        wnoutrefresh(stdscr); // stage stdscr changes
        pm_update(); // update panels and flush
        int fetched = 0; // limit DB window fetches to once per frame
        int final_vdir = 0; // -1 up, +1 down
        int vcount = 0;     // count of vertical keypresses this frame
        int quit = 0;
        while ((ch = getch()) != ERR) {
            if (ch == KEY_RESIZE) { pm_on_resize(); continue; }

            if (search_mode) {
            // In search mode, arrow keys navigate matches, ESC exits
            if (ch == KEY_LEFT || ch == KEY_UP) {
                if (search_hit_count > 0) {
                    search_hit_index = (search_hit_index > 0) ? (search_hit_index - 1) : (search_hit_count - 1);
                    cursor_row = hits[search_hit_index].row;
                    cursor_col = hits[search_hit_index].col;
                    search_sel_start = hits[search_hit_index].start;
                    search_sel_len = hits[search_hit_index].len;
                    if (rows_visible > 0) row_page = cursor_row / rows_visible;
                }
            } else if (ch == KEY_RIGHT || ch == KEY_DOWN) {
                if (search_hit_count > 0) {
                    search_hit_index = (search_hit_index + 1) % (search_hit_count > 0 ? search_hit_count : 1);
                    cursor_row = hits[search_hit_index].row;
                    cursor_col = hits[search_hit_index].col;
                    search_sel_start = hits[search_hit_index].start;
                    search_sel_len = hits[search_hit_index].len;
                    if (rows_visible > 0) row_page = cursor_row / rows_visible;
                }
            } else if (ch == 27) { // ESC
                exit_search();
            }
        } else if (!editing_mode) {
            if (ch == 'q' || ch == 'Q')
                { quit = 1; break; }
            else if (ch == 8 /* Ctrl+H */ || ch == KEY_HOME) {
                // Go to top-left of dataset; in seek mode, fetch first window
                col_page = 0; row_page = 0; cursor_col = 0; cursor_row = -1;
                if (low_ram_mode && seek_mode_active()) {
                    char err[128]={0}; int page=(rows_visible>0?rows_visible:200);
                    seek_mode_fetch_first(table, page, err, sizeof err);
                }
            }
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
                // Focus top-left of current page (not table top)
                int start_row = row_page * (rows_visible > 0 ? rows_visible : 1);
                if (start_row < 0) start_row = 0;
                cursor_row = (start_row < table->row_count) ? start_row : (table->row_count > 0 ? table->row_count - 1 : -1);
                cursor_col = col_start;
            }
            else if (ch == 'm' || ch == 'M') {
                show_table_menu(table);
            } else if (ch == KEY_LEFT) {
                if (col_page > 0) col_page--; // page index
            } else if (ch == KEY_RIGHT) {
                if (col_page < total_pages - 1) col_page++; // page index
            } else if (ch == KEY_UP) {
                final_vdir = -1;
                vcount++;
                if (low_ram_mode && seek_mode_active()) {
                    if (cursor_row <= 0) {
                        char err[128]={0};
                        int page = (rows_visible>0?rows_visible:200);
                        if (!fetched && seek_mode_fetch_prev(table, page, err, sizeof err) > 0) {
                            fetched = 1;
                            // keep cursor near top
                            cursor_row = 0;
                        }
                    } else {
                        if (row_page > 0) row_page--; // keep legacy behavior when not at top
                    }
                } else {
                    if (row_page > 0) row_page--;
                }
            } else if (ch == KEY_DOWN) {
                final_vdir = +1;
                vcount++;
                if (low_ram_mode && seek_mode_active()) {
                    if (cursor_row >= table->row_count - 1) {
                        char err[128]={0};
                        int page = (rows_visible>0?rows_visible:200);
                        if (!fetched && seek_mode_fetch_next(table, page, err, sizeof err) > 0) {
                            fetched = 1;
                            // keep cursor near bottom
                            cursor_row = table->row_count - 1;
                        }
                    } else {
                        if (row_page < total_row_pages - 1) row_page++;
                    }
                } else {
                    if (row_page < total_row_pages - 1) row_page++;
                }
            }
        } else {
            // If in interactive delete modes, override edit controls for navigation + confirm
            if (del_row_mode) {
                if (table->row_count <= 0) { del_row_mode = 0; }
                switch (ch) {
                    case KEY_UP:
                        {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            if (rmin < 0) rmin = 0;
                            if (cursor_row > rmin) cursor_row--;
                        }
                        break;
                    case KEY_DOWN:
                        {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
                            if (rmax >= table->row_count) rmax = table->row_count - 1;
                            if (cursor_row < rmax) cursor_row++;
                        }
                        break;
                    case '\n':
                        confirm_delete_row_at(table, cursor_row);
                        if (cursor_row >= table->row_count) cursor_row = table->row_count - 1;
                        if (cursor_row < 0) { cursor_row = -1; editing_mode = (table->row_count > 0); }
                        del_row_mode = 0;
                        break;
                    case 27: // ESC cancels mode
                        del_row_mode = 0;
                        break;
                    // Allow moving columns within visible range to inspect, but not necessary
                    case KEY_LEFT:
                        if (cursor_col > col_start) cursor_col--;
                        break;
                    case KEY_RIGHT:
                        {
                            int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
                            if (cursor_col < cmax) cursor_col++;
                        }
                        break;
                    default:
                        break;
                }
                continue; // handled next input
            }
            if (del_col_mode) {
                if (table->column_count <= 0) { del_col_mode = 0; }
                switch (ch) {
                    case KEY_LEFT:
                        if (cursor_col > col_start) cursor_col--;
                        break;
                    case KEY_RIGHT:
                        {
                            int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
                            if (cursor_col < cmax) cursor_col++;
                        }
                        break;
                    case '\n':
                        confirm_delete_column_at(table, cursor_col);
                        if (cursor_col >= table->column_count) cursor_col = table->column_count - 1;
                        if (cursor_col < 0) cursor_col = 0;
                        del_col_mode = 0;
                        break;
                    case 27:
                        del_col_mode = 0; break;
                    case KEY_UP:
                    case KEY_DOWN:
                        // Allow moving rows to inspect context
                        if (ch == KEY_UP) {
                            if (cursor_row > -1) {
                                int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                                if (rmin < 0) rmin = 0;
                                if (cursor_row > rmin) cursor_row--; // bound to page top
                            }
                        } else if (ch == KEY_DOWN) {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
                            if (rmax >= table->row_count) rmax = table->row_count - 1;
                            if (cursor_row < rmax) cursor_row++;
                        }
                        break;
                    default:
                        break;
                }
                continue; // handled next input
            }
            switch (ch) {
                case KEY_LEFT:
                    // Do not page during edit; restrict to visible range
                    if (cursor_col > col_start) cursor_col--;
                    break;
                case KEY_RIGHT:
                    {
                        int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
                        if (cursor_col < cmax) cursor_col++;
                    }
                    break;
                case KEY_UP:
                    if (ch == 8 /* Ctrl+H */) { /* already handled */ }
                    final_vdir = -1;
                    vcount++;
                    if (low_ram_mode && seek_mode_active()) {
                        if (cursor_row <= 0) {
                            char err[128]={0}; int page=(rows_visible>0?rows_visible:200);
                            if (!fetched && seek_mode_fetch_prev(table, page, err, sizeof err) > 0) {
                                fetched = 1;
                                cursor_row = 0; // stay at top
                            } else {
                                if (cursor_row > -1) cursor_row--; // header
                            }
                        } else {
                            cursor_row--;
                        }
                    } else {
                        // Header allowed; restrict body to page
                        if (cursor_row == -1) {
                            // stay on header
                        } else {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            if (rmin < 0) rmin = 0;
                            if (cursor_row > rmin) cursor_row--;
                            else cursor_row = -1; // move to header when at top of page
                        }
                    }
                    break;
                case KEY_DOWN:
                    final_vdir = +1;
                    vcount++;
                    if (low_ram_mode && seek_mode_active()) {
                        if (cursor_row >= table->row_count - 1) {
                            char err[128]={0}; int page=(rows_visible>0?rows_visible:200);
                            if (!fetched && seek_mode_fetch_next(table, page, err, sizeof err) > 0) {
                                fetched = 1;
                                cursor_row = table->row_count - 1; // stay at bottom
                            }
                        } else {
                            cursor_row++;
                        }
                    } else {
                        // restrict to page bottom
                        int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                        int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
                        if (rmax >= table->row_count) rmax = table->row_count - 1;
                        if (cursor_row < rmax) cursor_row++;
                    }
                    break;
                case 27: // ESC key
                    editing_mode = 0;
                    break;
                case 8: // Ctrl+H: Home to top-left page
                case KEY_HOME:
                    col_page = 0; row_page = 0; cursor_col = col_start; cursor_row = (row_page * (rows_visible>0?rows_visible:1));
                    if (cursor_row >= table->row_count) cursor_row = table->row_count - 1;
                    if (cursor_row < 0) cursor_row = -1; // header if empty
                    if (low_ram_mode && seek_mode_active()) {
                        char err[128]={0}; int page=(rows_visible>0?rows_visible:200);
                        seek_mode_fetch_first(table, page, err, sizeof err);
                    }
                    break;
                case '\n': // Enter key
                    if (cursor_row == -1)
                        edit_header_cell(table, cursor_col);
                    else
                        edit_body_cell(table, cursor_row, cursor_col);
                    break;
                case 'x':
                    // Enter interactive row delete selection mode
                    if (table->row_count <= 0) { show_error_message("No rows to delete."); break; }
                    if (cursor_row < 0) cursor_row = 0;
                    del_row_mode = 1; del_col_mode = 0;
                    break;
                case 'X':
                    // Enter interactive column delete selection mode
                    if (table->column_count <= 0) { show_error_message("No columns to delete."); break; }
                    if (table->column_count == 1) { show_error_message("Cannot delete the last column."); break; }
                    if (cursor_col < 0) cursor_col = 0;
                    del_col_mode = 1; del_row_mode = 0;
                    break;
                case KEY_BACKSPACE:
                case 127: // some terms send DEL for backspace
                    if (cursor_row < 0) {
                        show_error_message("Move to a cell to clear.");
                    } else {
                        prompt_clear_cell(table, cursor_row, cursor_col);
                    }
                    break;
            }
        }
        }
        // Apply final directional cursor snap to page edge (edit mode only)
        if (editing_mode && final_vdir != 0 && rows_visible > 0 && vcount >= 3) {
            int rstart = row_page * rows_visible;
            int rend = rstart + rows_visible - 1;
            if (rend >= table->row_count) rend = table->row_count - 1;
            if (final_vdir > 0) {
                if (rend >= 0) cursor_row = rend;
            } else if (final_vdir < 0) {
                if (rstart < table->row_count) cursor_row = (rstart >= 0 ? rstart : 0);
            }
        }
        if (quit) break;
        // Final hard clamp to viewport
        clamp_cursor_viewport(table);
        napms(16); // ~60 FPS; coalesces many key repeats into one redraw
    }
}
