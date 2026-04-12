/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* UTF-8-aware text measurement and drawing helpers for ncurses windows. */

#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <stddef.h>
#include <ncurses.h>

/* Measure or render UTF-8 text while respecting terminal display width. */
int ui_text_width(const char *text);
/* Measure only the first byte_count bytes of a UTF-8 string. */
int ui_text_width_n(const char *text, size_t bytes);
/* Return how many bytes fit inside the requested terminal cell width. */
size_t ui_text_bytes_for_width(const char *text, int max_width);
/* Draw a bounded UTF-8 substring into a curses window. */
int ui_text_addnstr_width(WINDOW *win, const char *text, size_t bytes, int max_width);
/* Draw a full UTF-8 string into a curses window with width clipping. */
int ui_text_addstr_width(WINDOW *win, const char *text, int max_width);

#endif /* UI_TEXT_H */
