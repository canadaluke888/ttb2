/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared modal dialog primitives used across UI flows. */

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "ui/internal.h"
#include "ui/panel_manager.h"
#include "ui/dialog_internal.h"

mmask_t ui_mouse_wheel_up_mask(void)
{
    mmask_t mask = 0;

#ifdef BUTTON4_PRESSED
    mask |= BUTTON4_PRESSED;
#endif
#ifdef BUTTON4_RELEASED
    mask |= BUTTON4_RELEASED;
#endif
#ifdef BUTTON4_CLICKED
    mask |= BUTTON4_CLICKED;
#endif
#ifdef BUTTON4_DOUBLE_CLICKED
    mask |= BUTTON4_DOUBLE_CLICKED;
#endif
#ifdef BUTTON4_TRIPLE_CLICKED
    mask |= BUTTON4_TRIPLE_CLICKED;
#endif
    return mask;
}

mmask_t ui_mouse_wheel_down_mask(void)
{
    mmask_t mask = 0;

#ifdef BUTTON5_PRESSED
    mask |= BUTTON5_PRESSED;
#endif
#ifdef BUTTON5_RELEASED
    mask |= BUTTON5_RELEASED;
#endif
#ifdef BUTTON5_CLICKED
    mask |= BUTTON5_CLICKED;
#endif
#ifdef BUTTON5_DOUBLE_CLICKED
    mask |= BUTTON5_DOUBLE_CLICKED;
#endif
#ifdef BUTTON5_TRIPLE_CLICKED
    mask |= BUTTON5_TRIPLE_CLICKED;
#endif
    return mask;
}

void ui_dialog_clamp_list_view(int count, int visible, int *top, int *selected)
{
    int max_top;

    if (!top || !selected || count <= 0) return;
    if (visible < 1) visible = 1;

    if (*selected < 0) *selected = 0;
    if (*selected >= count) *selected = count - 1;
    if (*selected < *top) *top = *selected;
    if (*selected >= *top + visible) *top = *selected - visible + 1;

    max_top = (count > visible) ? (count - visible) : 0;
    if (*top < 0) *top = 0;
    if (*top > max_top) *top = max_top;
}

int ui_dialog_handle_list_mouse(WINDOW *win,
                                int ch,
                                int list_top_row,
                                int visible,
                                int count,
                                int *top,
                                int *selected,
                                int *activate,
                                int *nav_dir)
{
    MEVENT event;
    int win_y;
    int win_x;
    int win_h;
    int win_w;
    int row;
    int idx;

    if (activate) *activate = 0;
    if (nav_dir) *nav_dir = 0;
    if (!win || ch != KEY_MOUSE || count <= 0 || !top || !selected) return 0;
    if (getmouse(&event) != OK) return 1;

    getbegyx(win, win_y, win_x);
    getmaxyx(win, win_h, win_w);
    if (event.y < win_y || event.y >= win_y + win_h || event.x < win_x || event.x >= win_x + win_w) {
        return 1;
    }

    if (event.bstate & ui_mouse_wheel_up_mask()) {
        if (*selected > 0) (*selected)--;
        ui_dialog_clamp_list_view(count, visible, top, selected);
        if (nav_dir) *nav_dir = -1;
        return 1;
    }
    if (event.bstate & ui_mouse_wheel_down_mask()) {
        if (*selected < count - 1) (*selected)++;
        ui_dialog_clamp_list_view(count, visible, top, selected);
        if (nav_dir) *nav_dir = +1;
        return 1;
    }

    if (!(event.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON1_PRESSED))) return 1;

    row = event.y - win_y;
    if (row < list_top_row || row >= list_top_row + visible) return 1;

    idx = *top + (row - list_top_row);
    if (idx < 0 || idx >= count) return 1;

    *selected = idx;
    ui_dialog_clamp_list_view(count, visible, top, selected);
    if (activate) *activate = 1;
    return 1;
}

int ui_dialog_show_simple_list_modal(const char *title, const char **items, int count, int initial_selected)
{
    int prev_vis = curs_set(0);
    int h = count + 4;
    int w;
    int y;
    int x;
    int selected;
    int visible;
    int top = 0;
    int ch;

    noecho();
    if (h < 8) h = 8;
    if (h > LINES - 2) h = LINES - 2;
    w = COLS - 4;
    if (w < 20) w = COLS - 2;
    if (w < 20) w = 20;
    y = (LINES - h) / 2;
    x = (COLS - w) / 2;

    {
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);

        keypad(modal->win, TRUE);
        selected = (initial_selected >= 0 && initial_selected < count) ? initial_selected : 0;
        if (count <= 0) selected = -1;
        visible = h - 4;
        if (visible < 1) visible = 1;
        if (selected >= visible) top = selected - visible + 1;

        while (1) {
            int drawn = 0;

            werase(modal->win);
            box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "%s", title ? title : "");
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

            for (; drawn < visible && (top + drawn) < count; ++drawn) {
                int idx = top + drawn;
                int row = 3 + drawn;

                if (idx == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win, row, 2, "%s", items[idx]);
                if (idx == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }

            for (int i = drawn; i < visible; ++i) {
                int row = 3 + i;

                if (row >= h - 1) break;
                mvwhline(modal->win, row, 1, ' ', w - 2);
            }

            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            ch = wgetch(modal->win);
            if (ch == KEY_MOUSE) {
                int activate = 0;
                int nav_dir = 0;

                if (ui_dialog_handle_list_mouse(modal->win, ch, 3, visible, count, &top, &selected, &activate, &nav_dir)) {
                    if (activate) break;
                    continue;
                }
            } else if (ch == KEY_UP) {
                if (count > 0) {
                    selected = (selected > 0) ? selected - 1 : count - 1;
                    ui_dialog_clamp_list_view(count, visible, &top, &selected);
                }
            } else if (ch == KEY_DOWN) {
                if (count > 0) {
                    selected = (selected + 1) % count;
                    ui_dialog_clamp_list_view(count, visible, &top, &selected);
                }
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
    }

    if (prev_vis != -1) curs_set(prev_vis);
    return selected;
}

int ui_dialog_show_save_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz)
{
    return show_text_input_modal(title, "[Enter] Save   [Esc] Back", prompt, out, out_sz, false);
}

int ui_dialog_path_has_extension(const char *name, const char *ext)
{
    size_t len_name;
    size_t len_ext;

    if (!name || !ext) return 0;
    len_name = strlen(name);
    len_ext = strlen(ext);
    if (len_ext > len_name) return 0;
    return strcasecmp(name + len_name - len_ext, ext) == 0;
}

int ui_dialog_build_output_path(char *outpath, size_t outpath_sz, const char *dir, const char *filename, const char *ext)
{
    size_t used;
    size_t ext_len;

    if (!outpath || outpath_sz == 0 || !dir || !*dir || !filename || !*filename || !ext) return -1;
    if (snprintf(outpath, outpath_sz, "%s/%s", dir, filename) >= (int)outpath_sz) return -1;
    if (ui_dialog_path_has_extension(outpath, ext)) return 0;

    used = strlen(outpath);
    ext_len = strlen(ext);
    if (used + ext_len + 1 > outpath_sz) return -1;
    memcpy(outpath + used, ext, ext_len + 1);
    return 0;
}
