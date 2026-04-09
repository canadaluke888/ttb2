#define _XOPEN_SOURCE 700

#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#include "ui_text.h"

static int decode_width(const char *text, size_t bytes, size_t *consumed)
{
    mbstate_t st;
    wchar_t wc;
    size_t rc;
    int width;

    memset(&st, 0, sizeof(st));
    rc = mbrtowc(&wc, text, bytes, &st);
    if (rc == (size_t)-2 || rc == (size_t)-1) {
        if (consumed) *consumed = 1;
        return 1;
    }
    if (rc == 0) {
        if (consumed) *consumed = 1;
        return 0;
    }

    width = wcwidth(wc);
    if (width < 0) width = 1;
    if (consumed) *consumed = rc;
    return width;
}

int ui_text_width_n(const char *text, size_t bytes)
{
    size_t pos = 0;
    int width = 0;

    if (!text) return 0;
    while (pos < bytes && text[pos]) {
        size_t consumed = 0;
        width += decode_width(text + pos, bytes - pos, &consumed);
        pos += consumed ? consumed : 1;
    }
    return width;
}

int ui_text_width(const char *text)
{
    return text ? ui_text_width_n(text, strlen(text)) : 0;
}

size_t ui_text_bytes_for_width(const char *text, int max_width)
{
    size_t pos = 0;
    int width = 0;
    size_t len;

    if (!text || max_width <= 0) return 0;
    len = strlen(text);
    while (pos < len && text[pos]) {
        size_t consumed = 0;
        int ch_width = decode_width(text + pos, len - pos, &consumed);
        if (width + ch_width > max_width) break;
        width += ch_width;
        pos += consumed ? consumed : 1;
    }
    return pos;
}

int ui_text_addnstr_width(WINDOW *win, const char *text, size_t bytes, int max_width)
{
    size_t clipped;
    int width;

    if (!win || !text || max_width <= 0 || bytes == 0) return 0;
    clipped = ui_text_bytes_for_width(text, max_width);
    if (clipped > bytes) {
        clipped = bytes;
        while (clipped > 0 && ui_text_width_n(text, clipped) > max_width) {
            clipped--;
        }
    }
    if (clipped > 0) waddnstr(win, text, (int)clipped);
    width = ui_text_width_n(text, clipped);
    return width;
}

int ui_text_addstr_width(WINDOW *win, const char *text, int max_width)
{
    if (!text) return 0;
    return ui_text_addnstr_width(win, text, strlen(text), max_width);
}
