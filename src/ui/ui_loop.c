#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "table.h"
#include "ui.h"
#include "errors.h"  // Added to provide declaration for show_error_message
#include "panel_manager.h"
#include "workspace.h"
#include "db_manager.h"
#include "table_ops.h"
#include "ui_history.h"

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
int footer_page = 0;
UiReorderMode reorder_mode = UI_REORDER_NONE;
int reorder_source_row = -1;
int reorder_source_col = -1;
TableView ui_table_view;

// Local search state (current in-memory table/window)
typedef struct { int row; int col; int start; int len; } SearchHit;
static SearchHit *hits = NULL;
char search_query[128];
int search_sel_start = -1;
int search_sel_len = 0;

static void exit_search(void);

static int ui_reorder_active(void)
{
    return reorder_mode != UI_REORDER_NONE;
}

static void clear_reorder_mode(void)
{
    reorder_mode = UI_REORDER_NONE;
    reorder_source_row = -1;
    reorder_source_col = -1;
}

static void advance_footer_page(void)
{
    footer_page = (footer_page + 1) % 2;
}

static void ensure_cursor_column_visible(const Table *table)
{
    if (!table || table->column_count <= 0) {
        col_page = 0;
        return;
    }

    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col >= table->column_count) cursor_col = table->column_count - 1;

    while (cols_visible > 0 && cursor_col < col_start && col_page > 0) {
        col_page--;
    }
    while (cols_visible > 0 && cursor_col >= col_start + cols_visible && col_page < total_pages - 1) {
        col_page++;
    }
}

static void ensure_cursor_row_visible(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0 || rows_visible <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }

    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
    row_page = cursor_row / rows_visible;
}

static void move_cursor_left_paged(const Table *table)
{
    if (!table || table->column_count <= 0) return;
    if (cursor_col > 0) cursor_col--;
    ensure_cursor_column_visible(table);
}

static void move_cursor_right_paged(const Table *table)
{
    if (!table || table->column_count <= 0) return;
    if (cursor_col < table->column_count - 1) cursor_col++;
    ensure_cursor_column_visible(table);
}

static void move_cursor_up_paged(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }
    if (cursor_row > 0) cursor_row--;
    ensure_cursor_row_visible(table);
}

static void move_cursor_down_paged(Table *table)
{
    int visible_rows = ui_visible_row_count(table);

    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        return;
    }
    if (cursor_row < visible_rows - 1) cursor_row++;
    ensure_cursor_row_visible(table);
}

static void clear_search_hits(void) {
    if (hits) { free(hits); hits = NULL; }
    search_hit_count = 0; search_hit_index = 0;
    search_query[0] = '\0';
    search_sel_start = -1;
    search_sel_len = 0;
}

int ui_table_view_is_active(void)
{
    return ui_table_view.filter_active || ui_table_view.sort_active;
}

int ui_visible_row_count(Table *table)
{
    return tableview_visible_row_count(table, &ui_table_view);
}

int ui_actual_row_for_visible(Table *table, int visible_row)
{
    return tableview_row_to_actual(table, &ui_table_view, visible_row);
}

int ui_rebuild_table_view(Table *table, char *err, size_t err_sz)
{
    int rc = tableview_rebuild(table, &ui_table_view, err, err_sz);
    int visible_rows = ui_visible_row_count(table);

    if (rc != 0) return rc;
    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
    } else {
        if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
        if (cursor_row >= 0 && rows_visible > 0) row_page = cursor_row / rows_visible;
    }
    return 0;
}

void ui_reset_table_view(Table *table)
{
    (void)table;
    exit_search();
    clear_reorder_mode();
    ui_history_reset();
    footer_page = 0;
    tableview_free(&ui_table_view);
    cursor_row = -1;
    cursor_col = 0;
    col_page = 0;
    row_page = 0;
}

void ui_focus_location(Table *table, int actual_row, int col, int prefer_header)
{
    int visible_row;
    int visible_rows;

    if (!table) return;

    if (table->column_count <= 0) {
        cursor_col = 0;
        cursor_row = -1;
        col_page = 0;
        row_page = 0;
        return;
    }

    if (col < 0) col = 0;
    if (col >= table->column_count) col = table->column_count - 1;
    cursor_col = col;

    if (prefer_header) {
        cursor_row = -1;
        ensure_cursor_column_visible(table);
        return;
    }

    visible_rows = ui_visible_row_count(table);
    if (visible_rows <= 0) {
        cursor_row = -1;
        row_page = 0;
        ensure_cursor_column_visible(table);
        return;
    }

    if (!ui_table_view_is_active()) {
        visible_row = actual_row;
    } else {
        visible_row = -1;
        for (int i = 0; i < visible_rows; ++i) {
            if (ui_actual_row_for_visible(table, i) == actual_row) {
                visible_row = i;
                break;
            }
        }
    }

    if (visible_row < 0) {
        visible_row = 0;
    }
    if (visible_row >= visible_rows) {
        visible_row = visible_rows - 1;
    }

    cursor_row = visible_row;
    ensure_cursor_row_visible(table);
    ensure_cursor_column_visible(table);
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
    int visible_rows = tableview_visible_row_count(table, &ui_table_view);

    if (search_mode) {
        if (table) {
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= table->column_count) cursor_col = (table->column_count > 0) ? (table->column_count - 1) : 0;
        }
        if (visible_rows <= 0) {
            cursor_row = -1;
        } else {
            if (cursor_row < 0) cursor_row = 0;
            if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
        }
        return;
    }

    if (ui_reorder_active()) {
        if (table) {
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= table->column_count) cursor_col = (table->column_count > 0) ? (table->column_count - 1) : 0;
        }
        if (visible_rows <= 0) {
            cursor_row = -1;
            row_page = 0;
        } else {
            if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                if (cursor_row < -1) cursor_row = -1;
            } else if (cursor_row < 0) {
                cursor_row = 0;
            }
            if (cursor_row >= visible_rows) cursor_row = visible_rows - 1;
            if (cursor_row >= 0 && rows_visible > 0) row_page = cursor_row / rows_visible;
        }
        return;
    }

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
    if (rmax >= visible_rows) rmax = visible_rows - 1;
    if (cursor_row >= 0) {
        if (cursor_row < rmin) cursor_row = rmin;
        if (cursor_row > rmax) cursor_row = rmax;
    }
    if (visible_rows <= 0) cursor_row = -1;
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

int ui_format_cell_value(const Table *t, int row, int col, char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return -1;
    buf[0] = '\0';
    if (!t || row < 0 || row >= t->row_count || col < 0 || col >= t->column_count) return -1;
    if (!t->rows[row].values || !t->rows[row].values[col]) return 0;

    if (t->columns[col].type == TYPE_INT)
        snprintf(buf, buf_sz, "%d", *(int *)t->rows[row].values[col]);
    else if (t->columns[col].type == TYPE_FLOAT)
        snprintf(buf, buf_sz, "%.2f", *(float *)t->rows[row].values[col]);
    else if (t->columns[col].type == TYPE_BOOL)
        snprintf(buf, buf_sz, "%s", (*(int *)t->rows[row].values[col]) ? "true" : "false");
    else
        snprintf(buf, buf_sz, "%s", (char *)t->rows[row].values[col]);

    return 0;
}

static void gather_search_hits(Table *t, const char *query) {
    clear_search_hits();
    int visible_rows = ui_visible_row_count(t);
    if (!t || t->column_count <= 0 || visible_rows <= 0) return;
    strncpy(search_query, query, sizeof(search_query)-1); search_query[sizeof(search_query)-1] = '\0';
    trim_ascii(search_query);
    if (search_query[0] == '\0') return;
    int cap = 0;
    for (int r = 0; r < visible_rows; ++r) {
        int actual_row = ui_actual_row_for_visible(t, r);
        if (actual_row < 0) continue;
        for (int c = 0; c < t->column_count; ++c) {
            char buf[128] = "";
            ui_format_cell_value(t, actual_row, c, buf, sizeof(buf));
            if (buf[0] == '\0') continue;
            int start = ci_find(buf, search_query);
            if (start >= 0) {
                if (search_hit_count == cap) { cap = cap ? cap * 2 : 16; hits = realloc(hits, sizeof(SearchHit) * cap); }
                hits[search_hit_count].row = r; hits[search_hit_count].col = c;
                hits[search_hit_count].start = start; hits[search_hit_count].len = (int)strlen(search_query);
                search_hit_count++;
            }
        }
    }
}

static int prompt_search_query(const char *title, const char *prompt, char *out, size_t out_sz) {
    return show_text_input_modal(title,
                                 "[Enter] Search   [Esc] Cancel",
                                 prompt,
                                 out,
                                 out_sz,
                                 false);
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
    tableview_init(&ui_table_view);

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
            else if (ch == 's' || ch == 'S') {
                char serr[256] = {0};
                if (workspace_manual_save(table, serr, sizeof(serr)) != 0) {
                    if (serr[0]) show_error_message(serr);
                    else show_error_message("Save failed.");
                }
                else {
                    show_error_message("Workspace saved.");
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
                footer_page = 0;
                clear_reorder_mode();
                // Focus top-left of current page (not table top)
                int start_row = row_page * (rows_visible > 0 ? rows_visible : 1);
                if (start_row < 0) start_row = 0;
                cursor_row = (start_row < ui_visible_row_count(table)) ? start_row : (ui_visible_row_count(table) > 0 ? ui_visible_row_count(table) - 1 : -1);
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
            if (ch == 's' || ch == 'S') {
                char serr[256] = {0};
                if (workspace_manual_save(table, serr, sizeof(serr)) != 0) {
                    if (serr[0]) show_error_message(serr);
                    else show_error_message("Save failed.");
                }
                else {
                    show_error_message("Workspace saved.");
                }
                continue;
            }
            if (ch == '\t') {
                advance_footer_page();
                continue;
            }
            if (ui_reorder_active()) {
                switch (ch) {
                    case KEY_LEFT:
                        move_cursor_left_paged(table);
                        break;
                    case KEY_RIGHT:
                        move_cursor_right_paged(table);
                        break;
                    case KEY_UP:
                        if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                            if (cursor_row > -1) cursor_row--;
                        } else {
                            move_cursor_up_paged(table);
                        }
                        break;
                    case KEY_DOWN:
                        if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                            int visible_rows = ui_visible_row_count(table);
                            if (cursor_row < visible_rows - 1) cursor_row++;
                        } else {
                            move_cursor_down_paged(table);
                        }
                        break;
                    case '\n':
                        if (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) {
                            int source_visible = reorder_source_row;
                            int source_actual = ui_actual_row_for_visible(table, source_visible);
                            int dest_visible = cursor_row;
                            int dest_actual = ui_actual_row_for_visible(table, dest_visible);
                            char err[256] = {0};

                            if (source_visible < 0 || dest_visible < 0 || source_actual < 0 || dest_actual < 0) {
                                show_error_message("Invalid row selection.");
                                clear_reorder_mode();
                                break;
                            }
                            if (source_visible == dest_visible) {
                                show_error_message("Source and destination row are the same.");
                                clear_reorder_mode();
                                break;
                            }

                            if (reorder_mode == UI_REORDER_MOVE_ROW) {
                                int placement = prompt_move_row_placement(table, source_actual, dest_actual);
                                if (placement == 0 || placement == 1) {
                                    UiHistoryApplyResult result = {0};
                                    if (ui_history_move_row(table, source_actual, dest_actual, placement == 1, &result, err, sizeof(err)) != 0) {
                                        show_error_message(err[0] ? err : "Failed to move row.");
                                    } else {
                                        if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                                            show_error_message(err);
                                        }
                                        clear_reorder_mode();
                                    }
                                }
                            } else {
                                UiHistoryApplyResult result = {0};
                                if (ui_history_swap_rows(table, source_actual, dest_actual, &result, err, sizeof(err)) != 0) {
                                    show_error_message(err[0] ? err : "Failed to swap rows.");
                                } else {
                                    if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                                        show_error_message(err);
                                    }
                                    clear_reorder_mode();
                                }
                            }
                        } else {
                            int source_col = reorder_source_col;
                            int dest_col = cursor_col;
                            char err[256] = {0};

                            if (source_col < 0 || dest_col < 0 || source_col >= table->column_count || dest_col >= table->column_count) {
                                show_error_message("Invalid column selection.");
                                clear_reorder_mode();
                                break;
                            }
                            if (source_col == dest_col) {
                                show_error_message("Source and destination column are the same.");
                                clear_reorder_mode();
                                break;
                            }

                            if (reorder_mode == UI_REORDER_MOVE_COL) {
                                int placement = prompt_move_column_placement(table, source_col, dest_col);
                                if (placement == 0 || placement == 1) {
                                    UiHistoryApplyResult result = {0};
                                    if (ui_history_move_column(table, source_col, dest_col, placement == 1, &result, err, sizeof(err)) != 0) {
                                        show_error_message(err[0] ? err : "Failed to move column.");
                                    } else {
                                        if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                                            show_error_message(err);
                                        }
                                        clear_reorder_mode();
                                    }
                                }
                            } else {
                                UiHistoryApplyResult result = {0};
                                if (ui_history_swap_columns(table, source_col, dest_col, &result, err, sizeof(err)) != 0) {
                                    show_error_message(err[0] ? err : "Failed to swap columns.");
                                } else {
                                    if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) {
                                        show_error_message(err);
                                    }
                                    clear_reorder_mode();
                                }
                            }
                        }
                        break;
                    case 27:
                        clear_reorder_mode();
                        break;
                    default:
                        break;
                }
                continue;
            }
            if (del_row_mode) {
                if (ui_visible_row_count(table) <= 0) { del_row_mode = 0; }
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
                            if (rmax >= ui_visible_row_count(table)) rmax = ui_visible_row_count(table) - 1;
                            if (cursor_row < rmax) cursor_row++;
                        }
                        break;
                    case '\n':
                        confirm_delete_row_at(table, ui_actual_row_for_visible(table, cursor_row));
                        if (cursor_row >= ui_visible_row_count(table)) cursor_row = ui_visible_row_count(table) - 1;
                        if (cursor_row < 0) { cursor_row = -1; editing_mode = (ui_visible_row_count(table) > 0); }
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
                            if (rmax >= ui_visible_row_count(table)) rmax = ui_visible_row_count(table) - 1;
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
                        if (rmax >= ui_visible_row_count(table)) rmax = ui_visible_row_count(table) - 1;
                        if (cursor_row < rmax) cursor_row++;
                    }
                    break;
                case 27: // ESC key
                    clear_reorder_mode();
                    footer_page = 0;
                    editing_mode = 0;
                    break;
                case 8: // Ctrl+H: Home to top-left page
                case KEY_HOME:
                    col_page = 0; row_page = 0; cursor_col = col_start; cursor_row = (row_page * (rows_visible>0?rows_visible:1));
                    if (cursor_row >= ui_visible_row_count(table)) cursor_row = ui_visible_row_count(table) - 1;
                    if (cursor_row < 0) cursor_row = -1; // header if empty
                    if (low_ram_mode && seek_mode_active()) {
                        char err[128]={0}; int page=(rows_visible>0?rows_visible:200);
                        seek_mode_fetch_first(table, page, err, sizeof err);
                    }
                    break;
                case 21: {
                    UiHistoryApplyResult result = {0};
                    char err[256] = {0};
                    if (ui_history_undo(table, &result, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Nothing to undo.");
                    } else if (ui_history_refresh(table, &result, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Failed to refresh after undo.");
                    }
                    break;
                }
                case 18: {
                    UiHistoryApplyResult result = {0};
                    char err[256] = {0};
                    if (ui_history_redo(table, &result, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Nothing to redo.");
                    } else if (ui_history_refresh(table, &result, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Failed to refresh after redo.");
                    }
                    break;
                }
                case 'f':
                case 'F':
                    enter_search(table);
                    break;
                case '\n': // Enter key
                    if (cursor_row == -1)
                        edit_header_cell(table, cursor_col);
                    else
                        edit_body_cell(table, ui_actual_row_for_visible(table, cursor_row), cursor_col);
                    break;
                case 'x':
                    // Enter interactive row delete selection mode
                    if (ui_visible_row_count(table) <= 0) { show_error_message("No rows to delete."); break; }
                    if (cursor_row < 0) cursor_row = 0;
                    clear_reorder_mode();
                    del_row_mode = 1; del_col_mode = 0;
                    break;
                case 'v':
                    if (cursor_row < 0) {
                        if (table->column_count <= 0) {
                            show_error_message("No columns to move.");
                            break;
                        }
                        clear_reorder_mode();
                        reorder_mode = UI_REORDER_MOVE_COL;
                        reorder_source_col = cursor_col;
                    } else {
                        if (ui_visible_row_count(table) <= 0) {
                            show_error_message("No rows to move.");
                            break;
                        }
                        clear_reorder_mode();
                        reorder_mode = UI_REORDER_MOVE_ROW;
                        reorder_source_row = cursor_row;
                    }
                    del_row_mode = 0;
                    del_col_mode = 0;
                    break;
                case '[': {
                    int insert_row = 0;
                    if (table->column_count <= 0) {
                        show_error_message("Add at least one column first.");
                        break;
                    }
                    if (cursor_row >= 0) {
                        insert_row = ui_actual_row_for_visible(table, cursor_row);
                        if (insert_row < 0) insert_row = 0;
                    } else if (ui_visible_row_count(table) > 0) {
                        insert_row = ui_actual_row_for_visible(table, 0);
                        if (insert_row < 0) insert_row = 0;
                    }
                    prompt_insert_row_at(table, insert_row);
                    break;
                }
                case ']': {
                    int insert_row = 0;
                    if (table->column_count <= 0) {
                        show_error_message("Add at least one column first.");
                        break;
                    }
                    if (cursor_row >= 0) {
                        insert_row = ui_actual_row_for_visible(table, cursor_row);
                        if (insert_row < 0) insert_row = table->row_count;
                        else insert_row++;
                    } else if (ui_visible_row_count(table) > 0) {
                        insert_row = ui_actual_row_for_visible(table, 0);
                        if (insert_row < 0) insert_row = 0;
                    }
                    prompt_insert_row_at(table, insert_row);
                    break;
                }
                case 'X':
                    // Enter interactive column delete selection mode
                    if (table->column_count <= 0) { show_error_message("No columns to delete."); break; }
                    if (table->column_count == 1) { show_error_message("Cannot delete the last column."); break; }
                    if (cursor_col < 0) cursor_col = 0;
                    clear_reorder_mode();
                    del_col_mode = 1; del_row_mode = 0;
                    break;
                case 'V':
                    if (cursor_row < 0) {
                        if (table->column_count <= 0) {
                            show_error_message("No columns to swap.");
                            break;
                        }
                        clear_reorder_mode();
                        reorder_mode = UI_REORDER_SWAP_COL;
                        reorder_source_col = cursor_col;
                    } else {
                        if (ui_visible_row_count(table) <= 0) {
                            show_error_message("No rows to swap.");
                            break;
                        }
                        clear_reorder_mode();
                        reorder_mode = UI_REORDER_SWAP_ROW;
                        reorder_source_row = cursor_row;
                    }
                    del_row_mode = 0;
                    del_col_mode = 0;
                    break;
                case '{': {
                    int insert_col = (table->column_count > 0) ? cursor_col : 0;
                    if (insert_col < 0) insert_col = 0;
                    prompt_insert_column_at(table, insert_col);
                    break;
                }
                case '}': {
                    int insert_col = (table->column_count > 0) ? (cursor_col + 1) : 0;
                    if (insert_col < 0) insert_col = 0;
                    if (insert_col > table->column_count) insert_col = table->column_count;
                    prompt_insert_column_at(table, insert_col);
                    break;
                }
                case KEY_BACKSPACE:
                case 127: // some terms send DEL for backspace
                    if (cursor_row < 0) {
                        show_error_message("Move to a cell to clear.");
                    } else {
                        prompt_clear_cell(table, ui_actual_row_for_visible(table, cursor_row), cursor_col);
                    }
                    break;
            }
        }
        }
        // Apply final directional cursor snap to page edge (edit mode only)
        if (editing_mode && final_vdir != 0 && rows_visible > 0 && vcount >= 3) {
            int rstart = row_page * rows_visible;
            int rend = rstart + rows_visible - 1;
            if (rend >= ui_visible_row_count(table)) rend = ui_visible_row_count(table) - 1;
            if (final_vdir > 0) {
                if (rend >= 0) cursor_row = rend;
            } else if (final_vdir < 0) {
                if (rstart < ui_visible_row_count(table)) cursor_row = (rstart >= 0 ? rstart : 0);
            }
        }
        if (quit) break;
        // Final hard clamp to viewport
        clamp_cursor_viewport(table);
        napms(16); // ~60 FPS; coalesces many key repeats into one redraw
    }
}
