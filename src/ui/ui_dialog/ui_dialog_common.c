/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared modal dialog primitives used across UI flows. */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
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

static int ui_dialog_row_is_selectable(const UiDialogListRow *row)
{
    if (!row) return 0;
    return row->kind == UI_DIALOG_LIST_ROW_ITEM && row->enabled;
}

static int ui_dialog_find_row_by_id(const UiDialogListRow *rows, int count, int id)
{
    int i;

    if (!rows || count <= 0) return -1;
    for (i = 0; i < count; ++i) {
        if (rows[i].id == id && ui_dialog_row_is_selectable(&rows[i])) return i;
    }
    return -1;
}

static int ui_dialog_compute_modal_width(const UiDialogListOptions *options,
                                         const UiDialogListRow *rows,
                                         int count)
{
    int width = 20;
    int i;
    int max_width = COLS - 4;

    if (options && options->title) {
        int len = (int)strlen(options->title) + 4;

        if (len > width) width = len;
    }
    if (options && options->hint) {
        int len = (int)strlen(options->hint) + 4;

        if (len > width) width = len;
    }
    for (i = 0; i < count; ++i) {
        int len = (int)strlen(rows[i].label ? rows[i].label : "") + rows[i].indent + 4;

        if (len > width) width = len;
    }

    if (options && options->min_width > 0 && width < options->min_width) width = options->min_width;
    if (options && options->max_width > 0 && width > options->max_width) width = options->max_width;
    if (width > max_width) width = max_width;
    if (width < 20) width = 20;
    return width;
}

int ui_dialog_list_find_first_selectable(const UiDialogListRow *rows, int count)
{
    int i;

    if (!rows || count <= 0) return -1;
    for (i = 0; i < count; ++i) {
        if (ui_dialog_row_is_selectable(&rows[i])) return i;
    }
    return -1;
}

int ui_dialog_list_find_next_selectable(const UiDialogListRow *rows, int count, int start, int dir)
{
    int idx;
    int step;

    if (!rows || count <= 0 || dir == 0) return -1;
    idx = start;
    for (step = 0; step < count; ++step) {
        idx += dir;
        if (idx < 0) idx = count - 1;
        else if (idx >= count) idx = 0;
        if (ui_dialog_row_is_selectable(&rows[idx])) return idx;
    }
    return -1;
}

void ui_dialog_clamp_styled_list_view(const UiDialogListRow *rows, int count, int visible, int *top, int *selected)
{
    int max_top;

    if (!rows || count <= 0 || !top || !selected) return;
    if (visible < 1) visible = 1;

    if (*selected < 0 || *selected >= count || !ui_dialog_row_is_selectable(&rows[*selected])) {
        *selected = ui_dialog_list_find_first_selectable(rows, count);
    }
    if (*selected < 0) {
        *selected = 0;
        *top = 0;
        return;
    }

    if (*selected < *top) *top = *selected;
    if (*selected >= *top + visible) *top = *selected - visible + 1;

    max_top = (count > visible) ? (count - visible) : 0;
    if (*top < 0) *top = 0;
    if (*top > max_top) *top = max_top;
}

int ui_dialog_styled_list_row_at(int top,
                                 int visible,
                                 int list_top_row,
                                 int window_row,
                                 const UiDialogListRow *rows,
                                 int count)
{
    int idx;

    if (!rows || count <= 0 || visible <= 0) return -1;
    if (window_row < list_top_row || window_row >= list_top_row + visible) return -1;

    idx = top + (window_row - list_top_row);
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

int ui_dialog_show_styled_list_modal(const UiDialogListOptions *options, const UiDialogListRow *rows, int count)
{
    int prev_vis;
    int hint_rows;
    int h;
    int w;
    int x;
    int y;
    int body_top;
    int body_bottom;
    int visible;
    int top = 0;
    int selected = -1;
    int chosen_id = -1;
    int ch;

    if (!rows || count <= 0) return -1;

    prev_vis = curs_set(0);
    noecho();

    hint_rows = (options && options->hint && options->hint[0]) ? 1 : 0;
    h = count + 4 + ((options ? options->padding_y : 0) * 2) + hint_rows;
    if (h < 8) h = 8;
    if (h > LINES - 2) h = LINES - 2;
    w = ui_dialog_compute_modal_width(options, rows, count);
    x = (COLS - w) / 2;
    y = (LINES - h) / 2;
    if (x < 1) x = 1;
    if (y < 0) y = 0;

    body_top = 3 + (options ? options->padding_y : 0);
    body_bottom = h - 2 - (options ? options->padding_y : 0);
    if (hint_rows > 0) body_bottom--;
    visible = body_bottom - body_top + 1;
    if (visible < 1) visible = 1;

    if (options && options->initial_selected_id >= 0) {
        selected = ui_dialog_find_row_by_id(rows, count, options->initial_selected_id);
    }
    if (selected < 0) selected = ui_dialog_list_find_first_selectable(rows, count);
    ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);

    {
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);

        keypad(modal->win, TRUE);
        while (1) {
            int win_row;
            int drawn = 0;

            werase(modal->win);
            box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "%s", (options && options->title) ? options->title : "");
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
            mvwaddch(modal->win, 2, 0, ACS_LTEE);
            mvwaddch(modal->win, 2, w - 1, ACS_RTEE);

            for (win_row = body_top; win_row <= body_bottom; ++win_row) {
                mvwhline(modal->win, win_row, 1, ' ', w - 2);
            }

            for (; drawn < visible && (top + drawn) < count; ++drawn) {
                int idx = top + drawn;
                int row = body_top + drawn;
                int padding_x = options ? options->padding_x : 0;
                int content_x = 2 + padding_x + rows[idx].indent;
                int content_w = w - content_x - 2 - padding_x;
                const char *label = rows[idx].label ? rows[idx].label : "";

                if (content_w < 1) content_w = 1;
                if (rows[idx].kind == UI_DIALOG_LIST_ROW_HEADER) {
                    int heading_x = (w - (int)strlen(label)) / 2;

                    if (heading_x < 2 + padding_x) heading_x = 2 + padding_x;
                    wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
                    mvwprintw(modal->win, row, heading_x, "%.*s", w - heading_x - 2, label);
                    wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
                } else if (rows[idx].kind == UI_DIALOG_LIST_ROW_DIVIDER) {
                    if (options && options->show_lines) {
                        mvwhline(modal->win, row, 2 + padding_x, ACS_HLINE, w - 4 - (padding_x * 2));
                    }
                } else if (rows[idx].kind == UI_DIALOG_LIST_ROW_ITEM) {
                    if (idx == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                    mvwprintw(modal->win, row, content_x, "%.*s", content_w, label);
                    if (idx == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
                }
            }

            if (hint_rows > 0) {
                wattron(modal->win, COLOR_PAIR(4));
                mvwprintw(modal->win, h - 2, 2, "%.*s", w - 4, options->hint);
                wattroff(modal->win, COLOR_PAIR(4));
            }

            pm_wnoutrefresh(shadow);
            pm_wnoutrefresh(modal);
            pm_update();

            ch = wgetch(modal->win);
            if (ch == KEY_MOUSE) {
                MEVENT event;

                if (getmouse(&event) == OK) {
                    int win_y;
                    int win_x;
                    int win_h;
                    int win_w;

                    getbegyx(modal->win, win_y, win_x);
                    getmaxyx(modal->win, win_h, win_w);
                    if (event.y >= win_y && event.y < win_y + win_h &&
                        event.x >= win_x && event.x < win_x + win_w) {
                        if (event.bstate & ui_mouse_wheel_up_mask()) {
                            int next = ui_dialog_list_find_next_selectable(rows, count, selected, -1);

                            if (next >= 0) {
                                selected = next;
                                ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);
                            }
                            continue;
                        }
                        if (event.bstate & ui_mouse_wheel_down_mask()) {
                            int next = ui_dialog_list_find_next_selectable(rows, count, selected, +1);

                            if (next >= 0) {
                                selected = next;
                                ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);
                            }
                            continue;
                        }
                        if (event.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON1_PRESSED)) {
                            int idx = ui_dialog_styled_list_row_at(top, visible, body_top, event.y - win_y, rows, count);

                            if (idx >= 0 && ui_dialog_row_is_selectable(&rows[idx])) {
                                selected = idx;
                                ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);
                                chosen_id = rows[selected].id;
                                break;
                            }
                            continue;
                        }
                    }
                }
            } else if (ch == KEY_UP) {
                int next = ui_dialog_list_find_next_selectable(rows, count, selected, -1);

                if (next >= 0) {
                    selected = next;
                    ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);
                }
            } else if (ch == KEY_DOWN) {
                int next = ui_dialog_list_find_next_selectable(rows, count, selected, +1);

                if (next >= 0) {
                    selected = next;
                    ui_dialog_clamp_styled_list_view(rows, count, visible, &top, &selected);
                }
            } else if (ch == '\n') {
                if (selected >= 0 && selected < count && ui_dialog_row_is_selectable(&rows[selected])) {
                    chosen_id = rows[selected].id;
                }
                break;
            } else if (ch == 27) {
                chosen_id = -1;
                break;
            }
        }

        pm_remove(modal);
        pm_remove(shadow);
        pm_update();
    }

    if (prev_vis != -1) curs_set(prev_vis);
    return chosen_id;
}

int ui_dialog_show_simple_list_modal(const char *title, const char **items, int count, int initial_selected)
{
    UiDialogListOptions options;
    UiDialogListRow *rows;
    int i;
    int result;

    if (!items || count <= 0) return -1;

    rows = (UiDialogListRow *)calloc((size_t)count, sizeof(*rows));
    if (!rows) return -1;

    for (i = 0; i < count; ++i) {
        rows[i].kind = UI_DIALOG_LIST_ROW_ITEM;
        rows[i].label = items[i];
        rows[i].id = i;
        rows[i].enabled = 1;
        rows[i].indent = 0;
    }

    memset(&options, 0, sizeof(options));
    options.title = title;
    options.initial_selected_id = initial_selected;
    options.min_width = 20;
    options.padding_x = 0;
    options.padding_y = 0;
    options.show_lines = false;
    options.paged = true;

    result = ui_dialog_show_styled_list_modal(&options, rows, count);
    free(rows);
    return result;
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
