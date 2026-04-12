#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "table.h"
#include "ui.h"
#include "ui_text.h"

static void add_repeat(const char *text, int count)
{
    for (int i = 0; i < count; ++i) addstr(text);
}

static void add_spaces(int count)
{
    for (int i = 0; i < count; ++i) addch(' ');
}

static int draw_cell_text(const char *text, int width, int color_pair)
{
    int used = 0;

    addch(' ');
    used++;
    attron(COLOR_PAIR(color_pair));
    used += ui_text_addstr_width(stdscr, text ? text : "", width - used);
    attroff(COLOR_PAIR(color_pair));
    if (used < width) add_spaces(width - used);
    return width;
}

static int numeric_column_uses_sign_slot(const Table *t, int col)
{
    if (!t || col < 0 || col >= t->column_count) return 0;
    return t->columns[col].type == TYPE_INT || t->columns[col].type == TYPE_FLOAT;
}

static int numeric_text_width_for_grid(const char *text)
{
    int width = 0;

    if (!text || !*text) return 0;
    width = 2; /* sign slot plus gap before digits */
    width += ui_text_width((*text == '-') ? text + 1 : text);
    return width;
}

static int draw_numeric_cell_text(const char *text, int width, int color_pair)
{
    int used = 0;
    int is_negative = text && text[0] == '-';
    const char *digits = is_negative ? text + 1 : (text ? text : "");
    int digits_width = ui_text_width(digits);
    int remaining;

    addch(' ');
    used++;

    remaining = width - used - digits_width - 2;
    if (remaining > 0) {
        add_spaces(remaining);
        used += remaining;
    }

    attron(COLOR_PAIR(color_pair));
    addch(is_negative ? '-' : ' ');
    addch(' ');
    used += 2;
    used += ui_text_addstr_width(stdscr, digits, width - used);
    attroff(COLOR_PAIR(color_pair));

    if (used < width) add_spaces(width - used);
    return width;
}

static void draw_highlighted_cell_text(const char *text, int width, int color_pair, int match_start, int match_len)
{
    size_t text_len;
    int used = 0;

    if (!text) text = "";
    text_len = strlen(text);
    if (match_start < 0 || match_len <= 0 || (size_t)match_start >= text_len) {
        draw_cell_text(text, width, color_pair);
        return;
    }

    addch(' ');
    used++;

    attron(COLOR_PAIR(color_pair));
    used += ui_text_addnstr_width(stdscr, text, (size_t)match_start, width - used);
    attroff(COLOR_PAIR(color_pair));

    if (used < width) {
        size_t match_bytes = (size_t)match_len;
        if ((size_t)match_start + match_bytes > text_len) match_bytes = text_len - (size_t)match_start;
        attron(COLOR_PAIR(10) | A_BOLD);
        used += ui_text_addnstr_width(stdscr, text + match_start, match_bytes, width - used);
        attroff(COLOR_PAIR(10) | A_BOLD);
    }

    if (used < width) {
        attron(COLOR_PAIR(color_pair));
        used += ui_text_addstr_width(stdscr, text + match_start + match_len, width - used);
        attroff(COLOR_PAIR(color_pair));
    }

    if (used < width) add_spaces(width - used);
}

static void draw_cell_value(const Table *t, int col, const char *text, int width, int color_pair)
{
    if (numeric_column_uses_sign_slot(t, col)) {
        draw_numeric_cell_text(text, width, color_pair);
        return;
    }

    draw_cell_text(text, width, color_pair);
}

static void draw_status_segment(int y, int *x, int max_x, int color_attr, const char *text)
{
    int remaining;

    if (!x || !text || !*text) return;
    if (*x > max_x) return;
    remaining = max_x - *x + 1;
    if (remaining <= 0) return;

    move(y, *x);
    attron(color_attr);
    *x += ui_text_addstr_width(stdscr, text, remaining);
    attroff(color_attr);
}

static void draw_action_hint_segment(int y, int *x, int max_x, const char *text)
{
    const char *cursor = text;

    if (!text) return;
    while (*cursor && *x <= max_x) {
        const char *open = strchr(cursor, '[');
        if (!open) {
            draw_status_segment(y, x, max_x, COLOR_PAIR(5), cursor);
            break;
        }
        if (open > cursor) {
            char plain[256];
            size_t len = (size_t)(open - cursor);
            if (len >= sizeof(plain)) len = sizeof(plain) - 1;
            memcpy(plain, cursor, len);
            plain[len] = '\0';
            draw_status_segment(y, x, max_x, COLOR_PAIR(5), plain);
        }
        {
            const char *close = strchr(open + 1, ']');
            if (!close) {
                draw_status_segment(y, x, max_x, COLOR_PAIR(5), open);
                break;
            }
            {
                char keybuf[256];
                char display_buf[256];
                int key_attr = COLOR_PAIR(7) | A_BOLD;
                size_t len = (size_t)(close - open - 1);
                if (len >= sizeof(keybuf)) len = sizeof(keybuf) - 1;
                memcpy(keybuf, open + 1, len);
                keybuf[len] = '\0';

                if (strcmp(keybuf, "Left Bracket") == 0) {
                    snprintf(display_buf, sizeof(display_buf), "[");
                    key_attr = COLOR_PAIR(10) | A_BOLD;
                } else if (strcmp(keybuf, "Right Bracket") == 0) {
                    snprintf(display_buf, sizeof(display_buf), "]");
                    key_attr = COLOR_PAIR(10) | A_BOLD;
                } else if (strcmp(keybuf, "{") == 0 || strcmp(keybuf, "}") == 0) {
                    snprintf(display_buf, sizeof(display_buf), "%s", keybuf);
                    key_attr = COLOR_PAIR(10) | A_BOLD;
                } else if (strcmp(keybuf, "v") == 0 || strcmp(keybuf, "V") == 0) {
                    snprintf(display_buf, sizeof(display_buf), "[%.253s]", keybuf);
                } else {
                    snprintf(display_buf, sizeof(display_buf), "%s", keybuf);
                }

                draw_status_segment(y, x, max_x, key_attr, display_buf);
            }
            cursor = close + 1;
        }
    }
}

static void draw_footer_page_indicator(int y, int *x, int max_x)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "Footer %d/2 [Tab] More", footer_page + 1);
    draw_status_segment(y, x, max_x, COLOR_PAIR(4) | A_BOLD, buf);
}

static void draw_footer_separator(int y, int *x, int max_x)
{
    draw_status_segment(y, x, max_x, COLOR_PAIR(8) | A_BOLD, "  |  ");
}

static void draw_footer_box(void)
{
    int left = 1;
    int right = COLS - 2;
    int top = LINES - 3;
    int bottom = LINES - 1;

    if (COLS < 4 || LINES < 4) return;

    attron(COLOR_PAIR(6));
    mvaddch(top, left, ACS_ULCORNER);
    mvhline(top, left + 1, ACS_HLINE, right - left - 1);
    mvaddch(top, right, ACS_URCORNER);
    mvvline(top + 1, left, ACS_VLINE, bottom - top - 1);
    mvvline(top + 1, right, ACS_VLINE, bottom - top - 1);
    mvaddch(bottom, left, ACS_LLCORNER);
    mvhline(bottom, left + 1, ACS_HLINE, right - left - 1);
    mvaddch(bottom, right, ACS_LRCORNER);
    attroff(COLOR_PAIR(6));
}

void draw_table_grid(Table *t) {
    int visible_row_count = ui_visible_row_count(t);

    if (t->column_count == 0)
    {
        total_pages = 1;
        cols_visible = 0;
        col_page = 0;
        col_start = 0;
        total_row_pages = 1;
        rows_visible = 0;
        row_page = 0;
        return;
    }

    int x = 2, y = 2;
    // Estimate rows visible to constrain width scan to current page (prevents O(N) scans)
    int grid_available_lines_est = LINES - 5;
    int rows_vis_est = (grid_available_lines_est - 3) / 2;
    if (rows_vis_est < 1) rows_vis_est = 1;
    int rstart_est = row_page * rows_vis_est; if (rstart_est < 0) rstart_est = 0; if (rstart_est > visible_row_count) rstart_est = visible_row_count;
    int rend_est = rstart_est + rows_vis_est; if (rend_est > visible_row_count) rend_est = visible_row_count;

    int *col_widths = malloc(t->column_count * sizeof(int));
    for (int j = 0; j < t->column_count; j++) {
        char header_buf[128];
        snprintf(header_buf, sizeof(header_buf), "%s (%s)", t->columns[j].name, type_to_string(t->columns[j].type));
        int max = ui_text_width(header_buf) + 2;
        // Only scan visible rows for width to avoid full-table cost
        for (int i = rstart_est; i < rend_est; i++) {
            int actual_row = ui_actual_row_for_visible(t, i);
            char buf[64];
            if (actual_row < 0) continue;
            ui_format_cell_value(t, actual_row, j, buf, sizeof(buf));
            int len = numeric_column_uses_sign_slot(t, j)
                        ? (numeric_text_width_for_grid(buf) + 2)
                        : (ui_text_width(buf) + 2);
            if (len > max) max = len;
        }
        col_widths[j] = max;
    }

    // Determine visible columns for current page to fit in COLS
    int available = COLS - x - 2; // some padding
    // Row-number gutter (compute early so column paging accounts for it)
    int use_gutter = row_gutter_enabled ? 1 : 0;
    long long base = 1;
    if (seek_mode_active()) base = seek_mode_row_base();
    long long max_row_num = base + (rows_vis_est > 0 ? rows_vis_est - 1 : 0);
    if (!seek_mode_active()) max_row_num = (long long)(rstart_est + (rows_vis_est > 0 ? rows_vis_est : 1));
    if (max_row_num < 1) max_row_num = 1;
    int gutter_digits = 1; long long tmpn = max_row_num;
    while (tmpn >= 10) { gutter_digits++; tmpn /= 10; }
    int gutter_w = use_gutter ? (gutter_digits + 2) : 0; // +2 for centering space
    if (use_gutter) {
        available -= (gutter_w + 1); // gutter + separator
        if (available < 10) available = 10;
    }

    // Precompute page starts based on widths
    int max_pages = t->column_count > 0 ? t->column_count : 1;
    int *page_starts = malloc(max_pages * sizeof(int));
    int pages = 0;
    int i = 0;
    while (i < t->column_count) {
        page_starts[pages++] = i;
        int wsum = 0;
        int j = i;
        int count = 0;
        while (j < t->column_count) {
            if (count > 0) {
                if (wsum + 1 + col_widths[j] > available) break; // +1 for separator
                wsum += 1 + col_widths[j];
            } else {
                if (col_widths[j] > available) { wsum = col_widths[j]; break; }
                wsum = col_widths[j];
            }
            count++;
            j++;
        }
        if (count <= 0) { count = 1; j = i + 1; }
        i = j;
    }

    // If in search mode, ensure the page shows the cursor column
    if ((search_mode || reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
        t->column_count > 0 && cursor_col >= 0 && cursor_col < t->column_count) {
        for (int p = 0; p < pages; ++p) {
            int s = page_starts[p];
            int wsum2 = 0, vis2 = 0;
            for (int j = s; j < t->column_count; ++j) {
                if (vis2 == 0) {
                    if (col_widths[j] > available) { vis2 = 1; break; }
                    wsum2 = col_widths[j];
                    vis2 = 1;
                } else {
                    if (wsum2 + 1 + col_widths[j] > available) break;
                    wsum2 += 1 + col_widths[j];
                    vis2++;
                }
            }
            int e = s + (vis2 > 0 ? vis2 : 1);
            if (cursor_col >= s && cursor_col < e) { col_page = p; break; }
        }
    }
    // Clamp page index and derive current start from page index
    if (col_page < 0) col_page = 0;
    if (col_page >= pages) col_page = (pages > 0 ? pages - 1 : 0);
    int start = (pages > 0 ? page_starts[col_page] : 0);
    // Compute visible count for chosen start
    int end = t->column_count;
    int wsum = 0; cols_visible = 0;
    for (int j = start; j < t->column_count; ++j) {
        if (cols_visible == 0) {
            if (col_widths[j] > available) { cols_visible = 1; break; }
            wsum = col_widths[j];
            cols_visible = 1;
        } else {
            if (wsum + 1 + col_widths[j] > available) break;
            wsum += 1 + col_widths[j];
            cols_visible++;
        }
    }
    if (cols_visible <= 0) cols_visible = 1;
    end = start + cols_visible; if (end > t->column_count) end = t->column_count;
    total_pages = pages > 0 ? pages : 1;
    col_start = start;

    // Gutter vars are already computed above (use_gutter, gutter_w, base)

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┏");
    if (use_gutter) {
        add_repeat("━", gutter_w);
        addstr((start < end) ? "┳" : "┓");
    }
    for (int j = start; j < end; j++) {
        add_repeat("━", col_widths[j]);
        addstr((j < end - 1) ? "┳" : "┓");
    }
    attroff(COLOR_PAIR(6));

    move(y++, x);
    attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    if (use_gutter) {
        // Gutter header cell: '#', centered
        int pad = gutter_w - 1; // one char '#'
        int lp = pad/2, rp = pad - lp;
        for (int i2 = 0; i2 < lp; ++i2) addch(' ');
        attron(COLOR_PAIR(3) | A_BOLD); addch('#'); attroff(COLOR_PAIR(3) | A_BOLD);
        for (int i2 = 0; i2 < rp; ++i2) addch(' ');
        attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    }
    for (int j = start; j < end; j++) {
        const char *name = t->columns[j].name;
        const char *type = type_to_string(t->columns[j].type);
        int remaining;
        int used = 0;
        extern int del_col_mode;
        int highlight_source = (editing_mode &&
                                (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
                                reorder_source_col == j);
        int highlight_dest_col = (editing_mode &&
                                  (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) &&
                                  cursor_col == j);
        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j) attron(A_REVERSE);
        if (editing_mode && del_col_mode && cursor_col == j) attron(A_REVERSE);
        if (highlight_dest_col) attron(A_REVERSE);
        if (highlight_source) attron(A_REVERSE);

        attron(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);
        addch(' ');
        used = 1;
        used += ui_text_addstr_width(stdscr, name, col_widths[j] - used);
        attroff(COLOR_PAIR(t->columns[j].color_pair_id) | A_BOLD);

        attron(COLOR_PAIR(3));
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, " (", remaining);
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, type, remaining);
        remaining = col_widths[j] - used;
        used += ui_text_addstr_width(stdscr, ")", remaining);
        attroff(COLOR_PAIR(3));

        if ((editing_mode || search_mode) && cursor_row == -1 && cursor_col == j) attroff(A_REVERSE);
        if (editing_mode && del_col_mode && cursor_col == j) attroff(A_REVERSE);
        if (highlight_dest_col) attroff(A_REVERSE);
        if (highlight_source) attroff(A_REVERSE);

        for (int s = used; s < col_widths[j]; s++)
            addch(' ');

        attron(COLOR_PAIR(6)); addstr("┃"); attroff(COLOR_PAIR(6));
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "┡");
    if (use_gutter) {
        add_repeat("━", gutter_w);
        addstr((start < end) ? "╇" : "┩");
    }
    for (int j = start; j < end; j++) {
        add_repeat("━", col_widths[j]);
        addstr((j < end - 1) ? "╇" : "┩");
    }
    attroff(COLOR_PAIR(6));

    // Calculate vertical paging capacity
    int grid_available_lines = LINES - 5; // space between title and boxed footer
    int max_rows = (grid_available_lines - 3) / 2; // from 2*N + 3 <= available
    if (max_rows < 1) max_rows = 1;
    rows_visible = max_rows;
    total_row_pages = (visible_row_count + rows_visible - 1) / rows_visible;
    // If in search mode, ensure the visible rows include the cursor row
    if (search_mode && cursor_row >= 0 && cursor_row < visible_row_count && rows_visible > 0) {
        row_page = cursor_row / rows_visible;
    }
    if (row_page >= total_row_pages) row_page = (total_row_pages > 0 ? total_row_pages - 1 : 0);
    int rstart = row_page * rows_visible;
    if (rstart < 0) rstart = 0;
    int rend = rstart + rows_visible;
    if (rend > visible_row_count) rend = visible_row_count;

    for (int i = rstart; i < rend; i++) {
        int actual_row = ui_actual_row_for_visible(t, i);
        move(y++, x);
        attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        if (use_gutter) {
            // Gutter row number centered
            long long rn = seek_mode_active() ? (base + (i - rstart)) : (long long)(i + 1);
            char buf[32]; snprintf(buf, sizeof(buf), "%lld", rn);
            int numlen = (int)strlen(buf);
            if (numlen > gutter_w) numlen = gutter_w; // clamp
            int pad = gutter_w - numlen;
            int lp = pad/2, rp = pad - lp;
            for (int p = 0; p < lp; ++p) addch(' ');
            attron(COLOR_PAIR(4)); addnstr(buf, numlen); attroff(COLOR_PAIR(4));
            for (int p = 0; p < rp; ++p) addch(' ');
            attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        }
        for (int j = start; j < end; j++) {
            char buf[64] = "";
            if (actual_row >= 0) ui_format_cell_value(t, actual_row, j, buf, sizeof(buf));

            extern int del_row_mode, del_col_mode;
            int highlight_cell = 0;
            int highlight_source = 0;
            if ((editing_mode || search_mode) && cursor_row == i && cursor_col == j) highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) && i == cursor_row)
                highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) && j == cursor_col)
                highlight_cell = 1;
            if (editing_mode && del_row_mode && i == cursor_row) highlight_cell = 1;
            if (editing_mode && del_col_mode && j == cursor_col) highlight_cell = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) && i == reorder_source_row)
                highlight_source = 1;
            if (editing_mode && (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) && j == reorder_source_col)
                highlight_source = 1;
            if (highlight_source) attron(A_REVERSE);
            if (highlight_cell) attron(A_REVERSE);

            // Draw cell with optional search substring highlight if selected in search mode
            if (search_mode && cursor_row == i && cursor_col == j) {
                if (numeric_column_uses_sign_slot(t, j)) {
                    draw_numeric_cell_text(buf, col_widths[j], t->columns[j].color_pair_id);
                } else {
                    draw_highlighted_cell_text(buf, col_widths[j], t->columns[j].color_pair_id, search_sel_start, search_sel_len);
                }
            } else {
                draw_cell_value(t, j, buf, col_widths[j], t->columns[j].color_pair_id);
            }

            if (highlight_cell) attroff(A_REVERSE);
            if (highlight_source) attroff(A_REVERSE);

            attron(COLOR_PAIR(6)); addstr("│"); attroff(COLOR_PAIR(6));
        }
        if (i < rend - 1) {
            move(y++, x);
            attron(COLOR_PAIR(6));
            addstr("├");
            if (use_gutter) {
                add_repeat("─", gutter_w);
                addstr((start < end) ? "┼" : "┤");
            }
            for (int j = start; j < end; j++) {
                add_repeat("─", col_widths[j]);
                addstr((j < end - 1) ? "┼" : "┤");
            }
            attroff(COLOR_PAIR(6));
        }
    }

    attron(COLOR_PAIR(6));
    mvprintw(y++, x, "└");
    if (use_gutter) {
        add_repeat("─", gutter_w);
        addstr((start < end) ? "┴" : "┘");
    }
    for (int j = start; j < end; j++) {
        add_repeat("─", col_widths[j]);
        addstr((j < end - 1) ? "┴" : "┘");
    }
    attroff(COLOR_PAIR(6));

    free(page_starts);
    free(col_widths);
}

void draw_ui(Table *table) {
    char view_buf[512];
    int max_x = COLS - 3;

    erase();

    int title_x = (COLS - ui_text_width(table->name)) / 2;
    if (title_x < 0) title_x = 0;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvaddnstr(0, title_x, table->name, (int)ui_text_bytes_for_width(table->name, COLS - title_x));
    attroff(COLOR_PAIR(1) | A_BOLD);

    // Show cursor position at top-left when in edit or search mode
    if (editing_mode || search_mode) {
        int rcur = (cursor_row < 0) ? 0 : (cursor_row + 1);
        int rtot = ui_visible_row_count(table);
        int ccur = (table->column_count > 0) ? (cursor_col + 1) : 0;
        int ctot = table->column_count;
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(0, 2, "R %d/%d  C %d/%d", rcur, rtot, ccur, ctot);
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    draw_table_grid(table);

    if (ui_table_view_is_active()) {
        attron(COLOR_PAIR(4));
        if (tableview_describe(table, &ui_table_view, view_buf, sizeof(view_buf)) != 0) {
            snprintf(view_buf, sizeof(view_buf), "View: %d/%d rows", ui_visible_row_count(table), table->row_count);
        }
        mvaddnstr(1, 2, view_buf, (int)ui_text_bytes_for_width(view_buf, COLS - 4));
        attroff(COLOR_PAIR(4));
    }

    draw_footer_box();

    // Footer: general hints vs paging hints colored differently
    int fy = LINES - 2;
    int fx = 2;
    if (search_mode) {
        draw_action_hint_segment(fy, &fx, max_x, "[←][→][↑][↓] Prev/Next Match");
        draw_footer_separator(fy, &fx, max_x);
        draw_action_hint_segment(fy, &fx, max_x, "[Esc] Exit Search");
        extern int search_hit_index; extern int search_hit_count;
        {
            char match_buf[64];
            draw_footer_separator(fy, &fx, max_x);
            snprintf(match_buf, sizeof(match_buf), "Matches %d/%d", (search_hit_count > 0 ? (search_hit_index + 1) : 0), search_hit_count);
            draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), match_buf);
        }
    } else if (!editing_mode) {
        draw_action_hint_segment(fy, &fx, max_x, "[C] Add Column  [R] Add Row");
        draw_footer_separator(fy, &fx, max_x);
        draw_action_hint_segment(fy, &fx, max_x, "[F] Search  [E] Edit Mode");
        draw_footer_separator(fy, &fx, max_x);
        draw_action_hint_segment(fy, &fx, max_x, "[M] Menu  [S] Save  [Q] Quit");
        draw_footer_separator(fy, &fx, max_x);
        draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+H] Home");
        if (ui_visible_row_count(table) == 0 && ui_table_view_is_active()) {
            draw_footer_separator(fy, &fx, max_x);
            draw_status_segment(fy, &fx, max_x, COLOR_PAIR(10) | A_BOLD, "0 results");
        }
        if (total_pages > 1 || total_row_pages > 1) {
            if (total_pages > 1) {
                char buf[64];
                draw_footer_separator(fy, &fx, max_x);
                snprintf(buf, sizeof(buf), "Cols Pg %d/%d [←][→] Columns", col_page + 1, total_pages);
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
            }
            if (total_row_pages > 1) {
                char buf[64];
                draw_footer_separator(fy, &fx, max_x);
                snprintf(buf, sizeof(buf), "Rows Pg %d/%d [↑][↓] Rows", row_page + 1, total_row_pages);
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
            }
        }
    } else {
        extern int del_row_mode, del_col_mode;
        if (del_row_mode) {
            draw_action_hint_segment(fy, &fx, max_x, "Del Row: [↑][↓] Select [Enter] Confirm [Esc] Cancel");
        } else if (del_col_mode) {
            draw_action_hint_segment(fy, &fx, max_x, "Del Col: [←][→] Select [Enter] Confirm [Esc] Cancel");
        } else if (reorder_mode == UI_REORDER_MOVE_ROW) {
            if (footer_page == 0) {
                draw_action_hint_segment(fy, &fx, max_x, "Move Row: [↑][↓] Destination [Enter] Place [Esc] Cancel");
            } else {
                draw_action_hint_segment(fy, &fx, max_x, "[Source] Highlighted [Cursor] Highlighted  Prompt: [Above] or [Below]");
            }
        } else if (reorder_mode == UI_REORDER_MOVE_COL) {
            if (footer_page == 0) {
                draw_action_hint_segment(fy, &fx, max_x, "Move Col: [←][→] Destination [Enter] Place [Esc] Cancel");
            } else {
                draw_action_hint_segment(fy, &fx, max_x, "[Source] Highlighted [Cursor] Highlighted  Prompt: [Left] or [Right]");
            }
        } else if (reorder_mode == UI_REORDER_SWAP_ROW) {
            if (footer_page == 0) {
                draw_action_hint_segment(fy, &fx, max_x, "Swap Row: [↑][↓] Destination [Enter] Confirm [Esc] Cancel");
            } else {
                draw_action_hint_segment(fy, &fx, max_x, "[Source] Highlighted [Cursor] Highlighted  Swap occurs after destination confirm");
            }
        } else if (reorder_mode == UI_REORDER_SWAP_COL) {
            if (footer_page == 0) {
                draw_action_hint_segment(fy, &fx, max_x, "Swap Col: [←][→] Destination [Enter] Confirm [Esc] Cancel");
            } else {
                draw_action_hint_segment(fy, &fx, max_x, "[Source] Highlighted [Cursor] Highlighted  Swap occurs after destination confirm");
            }
        } else {
            if (footer_page == 0) {
                draw_action_hint_segment(fy, &fx, max_x, "[Enter] Edit  [F] Search");
                draw_footer_separator(fy, &fx, max_x);
                draw_action_hint_segment(fy, &fx, max_x, "[X] Del Row  [Shift+X] Del Col");
                draw_footer_separator(fy, &fx, max_x);
                draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+U] Undo  [Ctrl+R] Redo");
                draw_footer_separator(fy, &fx, max_x);
                draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+H] Home  [M] Menu  [Esc] Exit");
            } else {
                draw_action_hint_segment(fy, &fx, max_x, "Insert: [Left Bracket] Above  [Right Bracket] Below");
                draw_footer_separator(fy, &fx, max_x);
                draw_action_hint_segment(fy, &fx, max_x, "[{] Left  [}] Right");
                draw_footer_separator(fy, &fx, max_x);
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(7) | A_BOLD, "V");
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(5), " Move  ");
                draw_action_hint_segment(fy, &fx, max_x, "[Shift+V] Swap");
            }
        }
        draw_footer_separator(fy, &fx, max_x);
        draw_footer_page_indicator(fy, &fx, max_x);
        if (ui_visible_row_count(table) == 0 && ui_table_view_is_active()) {
            draw_footer_separator(fy, &fx, max_x);
            draw_status_segment(fy, &fx, max_x, COLOR_PAIR(10) | A_BOLD, "0 results");
        }
        if (total_pages > 1 || total_row_pages > 1) {
            if (total_pages > 1) {
                char buf[32];
                draw_footer_separator(fy, &fx, max_x);
                snprintf(buf, sizeof(buf), "Cols Pg %d/%d", col_page + 1, total_pages);
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
            }
            if (total_row_pages > 1) {
                char buf[32];
                draw_footer_separator(fy, &fx, max_x);
                snprintf(buf, sizeof(buf), "Rows Pg %d/%d", row_page + 1, total_row_pages);
                draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
            }
        }
    }

    wnoutrefresh(stdscr);
}
