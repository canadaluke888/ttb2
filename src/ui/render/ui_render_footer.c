#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

void ui_draw_status_segment(int y, int *x, int max_x, int color_attr, const char *text)
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

void ui_draw_action_hint_segment(int y, int *x, int max_x, const char *text)
{
    const char *cursor = text;

    if (!text) return;
    while (*cursor && *x <= max_x) {
        const char *open = strchr(cursor, '[');

        if (!open) {
            ui_draw_status_segment(y, x, max_x, COLOR_PAIR(5), cursor);
            break;
        }
        if (open > cursor) {
            char plain[256];
            size_t len = (size_t)(open - cursor);

            if (len >= sizeof(plain)) len = sizeof(plain) - 1;
            memcpy(plain, cursor, len);
            plain[len] = '\0';
            ui_draw_status_segment(y, x, max_x, COLOR_PAIR(5), plain);
        }
        {
            const char *close = strchr(open + 1, ']');

            if (!close) {
                ui_draw_status_segment(y, x, max_x, COLOR_PAIR(5), open);
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

                ui_draw_status_segment(y, x, max_x, key_attr, display_buf);
            }
            cursor = close + 1;
        }
    }
}

void ui_draw_footer_page_indicator(int y, int *x, int max_x)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "Footer %d/2 [Tab] More", footer_page + 1);
    ui_draw_status_segment(y, x, max_x, COLOR_PAIR(4) | A_BOLD, buf);
}

void ui_draw_footer_separator(int y, int *x, int max_x)
{
    ui_draw_status_segment(y, x, max_x, COLOR_PAIR(8) | A_BOLD, "  |  ");
}

void ui_draw_footer_box(void)
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

void ui_draw_footer(Table *table)
{
    int fy = LINES - 2;
    int fx = 2;
    int max_x = COLS - 3;

    if (search_mode) {
        char match_buf[64];

        ui_draw_action_hint_segment(fy, &fx, max_x, "[←][→][↑][↓] Prev/Next Match");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Esc] Exit Search");
        ui_draw_footer_separator(fy, &fx, max_x);
        snprintf(match_buf, sizeof(match_buf), "Matches %d/%d", (search_hit_count > 0 ? (search_hit_index + 1) : 0), search_hit_count);
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), match_buf);
        return;
    }

    if (!editing_mode) {
        ui_draw_action_hint_segment(fy, &fx, max_x, "[C] Add Column  [R] Add Row");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[F] Search  [E] Edit Mode");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[M] Menu  [S] Save  [Q] Quit");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+H] Home");
        if (ui_visible_row_count(table) == 0 && ui_table_view_is_active()) {
            ui_draw_footer_separator(fy, &fx, max_x);
            ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(10) | A_BOLD, "0 results");
        }
        if (total_pages > 1) {
            char buf[64];

            ui_draw_footer_separator(fy, &fx, max_x);
            snprintf(buf, sizeof(buf), "Cols Pg %d/%d [←][→] Columns", col_page + 1, total_pages);
            ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
        }
        if (total_row_pages > 1) {
            char buf[64];

            ui_draw_footer_separator(fy, &fx, max_x);
            snprintf(buf, sizeof(buf), "Rows Pg %d/%d [↑][↓] Rows", row_page + 1, total_row_pages);
            ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
        }
        return;
    }

    if (del_row_mode) {
        ui_draw_action_hint_segment(fy, &fx, max_x, "Del Row: [↑][↓] Select [Enter] Confirm [Esc] Cancel");
    } else if (del_col_mode) {
        ui_draw_action_hint_segment(fy, &fx, max_x, "Del Col: [←][→] Select [Enter] Confirm [Esc] Cancel");
    } else if (reorder_mode == UI_REORDER_MOVE_ROW) {
        ui_draw_action_hint_segment(fy, &fx, max_x, footer_page == 0
            ? "Move Row: [↑][↓] Destination [Enter] Place [Esc] Cancel"
            : "[Source] Highlighted [Cursor] Highlighted  Prompt: [Above] or [Below]");
    } else if (reorder_mode == UI_REORDER_MOVE_COL) {
        ui_draw_action_hint_segment(fy, &fx, max_x, footer_page == 0
            ? "Move Col: [←][→] Destination [Enter] Place [Esc] Cancel"
            : "[Source] Highlighted [Cursor] Highlighted  Prompt: [Left] or [Right]");
    } else if (reorder_mode == UI_REORDER_SWAP_ROW) {
        ui_draw_action_hint_segment(fy, &fx, max_x, footer_page == 0
            ? "Swap Row: [↑][↓] Destination [Enter] Confirm [Esc] Cancel"
            : "[Source] Highlighted [Cursor] Highlighted  Swap occurs after destination confirm");
    } else if (reorder_mode == UI_REORDER_SWAP_COL) {
        ui_draw_action_hint_segment(fy, &fx, max_x, footer_page == 0
            ? "Swap Col: [←][→] Destination [Enter] Confirm [Esc] Cancel"
            : "[Source] Highlighted [Cursor] Highlighted  Swap occurs after destination confirm");
    } else if (footer_page == 0) {
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Enter] Edit  [F] Search");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[X] Del Row  [Shift+X] Del Col");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+U] Undo  [Ctrl+R] Redo");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Ctrl+H] Home  [M] Menu  [Esc] Exit");
    } else {
        ui_draw_action_hint_segment(fy, &fx, max_x, "Insert: [Left Bracket] Above  [Right Bracket] Below");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_action_hint_segment(fy, &fx, max_x, "[{] Left  [}] Right");
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(7) | A_BOLD, "V");
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(5), " Move  ");
        ui_draw_action_hint_segment(fy, &fx, max_x, "[Shift+V] Swap");
    }

    ui_draw_footer_separator(fy, &fx, max_x);
    ui_draw_footer_page_indicator(fy, &fx, max_x);
    if (ui_visible_row_count(table) == 0 && ui_table_view_is_active()) {
        ui_draw_footer_separator(fy, &fx, max_x);
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(10) | A_BOLD, "0 results");
    }
    if (total_pages > 1) {
        char buf[32];

        ui_draw_footer_separator(fy, &fx, max_x);
        snprintf(buf, sizeof(buf), "Cols Pg %d/%d", col_page + 1, total_pages);
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
    }
    if (total_row_pages > 1) {
        char buf[32];

        ui_draw_footer_separator(fy, &fx, max_x);
        snprintf(buf, sizeof(buf), "Rows Pg %d/%d", row_page + 1, total_row_pages);
        ui_draw_status_segment(fy, &fx, max_x, COLOR_PAIR(4), buf);
    }
}
