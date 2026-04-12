#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include "ui/internal.h"
#include "core/errors.h"

typedef struct {
    int row;
    int col;
    int start;
    int len;
} SearchHit;

static SearchHit *hits = NULL;

static void trim_ascii(char *s)
{
    int i;
    int n;

    if (!s) return;
    i = 0;
    while (s[i] && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int ci_find(const char *hay, const char *need)
{
    size_t nlen;

    if (!hay || !need) return -1;
    nlen = strlen(need);
    if (nlen == 0) return 0;
    for (int pos = 0; hay[pos]; ++pos) {
        size_t i = 0;

        while (hay[pos + i] && i < nlen) {
            char a = hay[pos + i];
            char b = need[i];

            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == nlen) return pos;
    }
    return -1;
}

static void clear_search_hits(void)
{
    if (hits) {
        free(hits);
        hits = NULL;
    }
    search_hit_count = 0;
    search_hit_index = 0;
    search_query[0] = '\0';
    search_sel_start = -1;
    search_sel_len = 0;
}

static void gather_search_hits(Table *table, const char *query)
{
    int visible_rows;
    int cap = 0;

    clear_search_hits();
    if (!table || table->column_count <= 0) return;

    visible_rows = ui_visible_row_count(table);
    if (visible_rows <= 0) return;

    strncpy(search_query, query, sizeof(search_query) - 1);
    search_query[sizeof(search_query) - 1] = '\0';
    trim_ascii(search_query);
    if (search_query[0] == '\0') return;

    for (int r = 0; r < visible_rows; ++r) {
        int actual_row = ui_actual_row_for_visible(table, r);

        if (actual_row < 0) continue;
        for (int c = 0; c < table->column_count; ++c) {
            char buf[128] = "";
            int start;

            ui_format_cell_value(table, actual_row, c, buf, sizeof(buf));
            if (buf[0] == '\0') continue;
            start = ci_find(buf, search_query);
            if (start >= 0) {
                if (search_hit_count == cap) {
                    SearchHit *next;

                    cap = cap ? cap * 2 : 16;
                    next = realloc(hits, sizeof(SearchHit) * cap);
                    if (!next) {
                        clear_search_hits();
                        return;
                    }
                    hits = next;
                }
                hits[search_hit_count].row = r;
                hits[search_hit_count].col = c;
                hits[search_hit_count].start = start;
                hits[search_hit_count].len = (int)strlen(search_query);
                search_hit_count++;
            }
        }
    }
}

void ui_search_enter(Table *table)
{
    char query[128] = {0};

    if (show_text_input_modal("Search", "[Enter] Search   [Esc] Cancel", "Query: ", query, sizeof(query), false) <= 0) return;
    gather_search_hits(table, query);
    if (search_hit_count <= 0) {
        show_error_message("No matches found.");
        return;
    }

    search_mode = 1;
    search_hit_index = 0;
    cursor_row = hits[0].row;
    cursor_col = hits[0].col;
    search_sel_start = hits[0].start;
    search_sel_len = hits[0].len;
    if (rows_visible > 0) row_page = cursor_row / rows_visible;
}

void ui_search_exit(void)
{
    search_mode = 0;
    clear_search_hits();
}

int ui_search_handle_key(Table *table, int ch)
{
    (void)table;

    if (!search_mode) return 0;

    if (ch == KEY_LEFT || ch == KEY_UP) {
        if (search_hit_count > 0) {
            search_hit_index = (search_hit_index > 0) ? (search_hit_index - 1) : (search_hit_count - 1);
            cursor_row = hits[search_hit_index].row;
            cursor_col = hits[search_hit_index].col;
            search_sel_start = hits[search_hit_index].start;
            search_sel_len = hits[search_hit_index].len;
            if (rows_visible > 0) row_page = cursor_row / rows_visible;
        }
        return 1;
    }

    if (ch == KEY_RIGHT || ch == KEY_DOWN) {
        if (search_hit_count > 0) {
            search_hit_index = (search_hit_index + 1) % search_hit_count;
            cursor_row = hits[search_hit_index].row;
            cursor_col = hits[search_hit_index].col;
            search_sel_start = hits[search_hit_index].start;
            search_sel_len = hits[search_hit_index].len;
            if (rows_visible > 0) row_page = cursor_row / rows_visible;
        }
        return 1;
    }

    if (ch == 27) {
        ui_search_exit();
        return 1;
    }

    return 0;
}
