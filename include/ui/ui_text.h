#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <stddef.h>
#include <ncurses.h>

int ui_text_width(const char *text);
int ui_text_width_n(const char *text, size_t bytes);
size_t ui_text_bytes_for_width(const char *text, int max_width);
int ui_text_addnstr_width(WINDOW *win, const char *text, size_t bytes, int max_width);
int ui_text_addstr_width(WINDOW *win, const char *text, int max_width);

#endif /* UI_TEXT_H */
