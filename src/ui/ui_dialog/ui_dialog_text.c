/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Reusable text-input dialogs for prompts and renaming flows. */

#include <ncurses.h>
#include <ctype.h>
#include <stdbool.h>
#include "ui/internal.h"
#include "ui/panel_manager.h"

int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty)
{
    int len = 0;
    int h;
    int w;
    int y;
    int x;
    int line_y = 2;
    int prompt_y = 3;
    int input_y = 4;
    int hint_y;
    int input_x = 4;
    int result = -1;

    if (!out || out_sz == 0) return -1;
    out[0] = '\0';

    noecho();
    curs_set(1);
    leaveok(stdscr, FALSE);

    h = 8;
    w = COLS - 6;
    if (w < 32) w = COLS - 2;
    if (w < 24) w = 24;
    y = (LINES - h) / 2;
    x = (COLS - w) / 2;
    hint_y = h - 2;

    {
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        int running = 1;

        keypad(modal->win, TRUE);

        while (running) {
            int field_width;
            int cursor_x;
            int ch;

            werase(modal->win);
            box(modal->win, 0, 0);

            if (title) {
                wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
                mvwprintw(modal->win, 1, 2, "%s", title);
                wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            }

            mvwhline(modal->win, line_y, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, line_y, 0, ACS_LTEE);
            mvwaddch(modal->win, line_y, w - 1, ACS_RTEE);
            mvwhline(modal->win, prompt_y, 1, ' ', w - 2);
            mvwhline(modal->win, input_y, 1, ' ', w - 2);
            mvwhline(modal->win, hint_y, 1, ' ', w - 2);

            if (prompt && prompt[0]) mvwprintw(modal->win, prompt_y, 2, "%s", prompt);

            if (input_x >= w - 2) input_x = w - 3;
            if (input_x < 2) input_x = 2;

            field_width = (w - 2) - input_x + 1;
            if (field_width < 0) field_width = 0;

            if (field_width > 0) {
                mvwaddch(modal->win, input_y, 2, '>');
                mvwprintw(modal->win, input_y, input_x, "%.*s", field_width, out);
                if (len < field_width) {
                    for (int i = len; i < field_width; ++i) {
                        mvwaddch(modal->win, input_y, input_x + i, ' ');
                    }
                }
            }

            if (hint && hint[0]) {
                wattron(modal->win, COLOR_PAIR(4));
                mvwprintw(modal->win, hint_y, 2, "%.*s", w - 4, hint);
                wattroff(modal->win, COLOR_PAIR(4));
            }

            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            cursor_x = input_x + len;
            if (cursor_x > w - 2) cursor_x = w - 2;
            if (cursor_x < input_x) cursor_x = input_x;
            wmove(modal->win, input_y, cursor_x);
            ch = wgetch(modal->win);

            if (ch == 27) {
                result = -1;
                break;
            } else if (ch == '\n' || ch == KEY_ENTER) {
                if (len > 0 || allow_empty) {
                    result = len;
                    break;
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == KEY_DC || ch == 8) {
                if (len > 0) {
                    len--;
                    out[len] = '\0';
                }
            } else if (ch == KEY_RESIZE) {
                result = -1;
                break;
            } else if (isprint(ch)) {
                if (len < (int)out_sz - 1) {
                    out[len++] = (char)ch;
                    out[len] = '\0';
                }
            }
        }

        pm_remove(modal);
        pm_remove(shadow);
        pm_update();
    }

    curs_set(0);
    leaveok(stdscr, TRUE);
    if (result < 0 && out_sz > 0) out[0] = '\0';
    return result;
}
