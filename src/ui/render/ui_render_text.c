/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Width-aware text rendering helpers for the UI. */

#include <ncurses.h>
#include <string.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

static int ui_exact_match_color_pair(int base_pair)
{
    switch (base_pair) {
        case 10: return 15; /* red -> cyan */
        case 11: return 14; /* green -> magenta */
        case 12: return 13; /* yellow -> blue */
        case 13: return 12; /* blue -> yellow */
        case 14: return 11; /* magenta -> green */
        case 15: return 10; /* cyan -> red */
        case 16: return 13; /* white -> blue */
        case 2:  return 10; /* default body white -> red */
        default: return 10;
    }
}

void ui_add_repeat(const char *text, int count)
{
    for (int i = 0; i < count; ++i) addstr(text);
}

void ui_add_spaces(int count)
{
    for (int i = 0; i < count; ++i) addch(' ');
}

int ui_draw_cell_text(const char *text, int width, int color_pair)
{
    int used = 0;

    addch(' ');
    used++;
    attron(COLOR_PAIR(color_pair));
    used += ui_text_addstr_width(stdscr, text ? text : "", width - used);
    attroff(COLOR_PAIR(color_pair));
    if (used < width) ui_add_spaces(width - used);
    return width;
}

int ui_draw_numeric_cell_text(const char *text, int width, int color_pair)
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
        ui_add_spaces(remaining);
        used += remaining;
    }

    attron(COLOR_PAIR(color_pair));
    addch(is_negative ? '-' : ' ');
    addch(' ');
    used += 2;
    used += ui_text_addstr_width(stdscr, digits, width - used);
    attroff(COLOR_PAIR(color_pair));

    if (used < width) ui_add_spaces(width - used);
    return width;
}

void ui_draw_highlighted_cell_text(const char *text, int width, int color_pair, int match_start, int match_len)
{
    size_t text_len;
    int used = 0;
    int highlight_pair = ui_exact_match_color_pair(color_pair);

    if (!text) text = "";
    text_len = strlen(text);
    if (match_start < 0 || match_len <= 0 || (size_t)match_start >= text_len) {
        ui_draw_cell_text(text, width, color_pair);
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
        attron(COLOR_PAIR(highlight_pair) | A_BOLD);
        used += ui_text_addnstr_width(stdscr, text + match_start, match_bytes, width - used);
        attroff(COLOR_PAIR(highlight_pair) | A_BOLD);
    }

    if (used < width) {
        attron(COLOR_PAIR(color_pair));
        used += ui_text_addstr_width(stdscr, text + match_start + match_len, width - used);
        attroff(COLOR_PAIR(color_pair));
    }

    if (used < width) ui_add_spaces(width - used);
}

void ui_draw_cell_value(const Table *table, int col, const char *text, int width, int color_pair)
{
    if (ui_numeric_column_uses_sign_slot(table, col)) {
        ui_draw_numeric_cell_text(text, width, color_pair);
        return;
    }

    ui_draw_cell_text(text, width, color_pair);
}
