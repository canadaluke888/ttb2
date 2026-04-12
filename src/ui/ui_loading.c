#include "ui/ui_loading.h"
#include "ui/panel_manager.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

struct UiLoadingModal {
    PmNode *shadow;
    PmNode *modal;
    double progress;
    int spinner_index;
    int indeterminate;
    int prev_cursor_state;
    char title[96];
    char message[256];
};

static void draw_loading_modal(struct UiLoadingModal *m);

static void progress_callback(void *ctx, double progress, const char *message)
{
    UiLoadingModal *modal = (UiLoadingModal *)ctx;
    ui_loading_modal_update(modal, progress, message);
}

UiLoadingModal *ui_loading_modal_start(const char *title,
                                       const char *initial_message,
                                       ProgressReporter *out_reporter)
{
    UiLoadingModal *modal = (UiLoadingModal *)calloc(1, sizeof(UiLoadingModal));
    if (!modal) {
        return NULL;
    }

    int max_h, max_w;
    getmaxyx(stdscr, max_h, max_w);

    int h = 9;
    int w = max_w - 8;
    if (w > 70) w = 70;
    if (w < 32) w = (max_w > 4) ? max_w - 2 : max_w;
    if (w < 24) w = 24;
    int y = (max_h - h) / 2;
    int x = (max_w - w) / 2;

    modal->shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    modal->modal = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    if (!modal->shadow || !modal->modal) {
        if (modal->modal) pm_remove(modal->modal);
        if (modal->shadow) pm_remove(modal->shadow);
        free(modal);
        return NULL;
    }

    strncpy(modal->title, title ? title : "Loading", sizeof(modal->title) - 1);
    modal->title[sizeof(modal->title) - 1] = '\0';

    if (initial_message && initial_message[0]) {
        strncpy(modal->message, initial_message, sizeof(modal->message) - 1);
        modal->message[sizeof(modal->message) - 1] = '\0';
    } else {
        strcpy(modal->message, "Working...");
    }

    modal->progress = 0.0;
    modal->spinner_index = 0;
    modal->indeterminate = 0;

    keypad(modal->modal->win, FALSE);
    modal->prev_cursor_state = curs_set(0);

    draw_loading_modal(modal);

    if (out_reporter) {
        out_reporter->update = progress_callback;
        out_reporter->ctx = modal;
    }

    return modal;
}

void ui_loading_modal_update(UiLoadingModal *modal,
                             double progress,
                             const char *message)
{
    if (!modal) {
        return;
    }

    if (message && message[0]) {
        strncpy(modal->message, message, sizeof(modal->message) - 1);
        modal->message[sizeof(modal->message) - 1] = '\0';
    }

    if (progress < 0.0) {
        modal->indeterminate = 1;
    } else {
        modal->indeterminate = 0;
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
        modal->progress = progress;
    }

    modal->spinner_index = (modal->spinner_index + 1) % 4;
    draw_loading_modal(modal);
}

void ui_loading_modal_finish(UiLoadingModal *modal)
{
    if (!modal) {
        return;
    }

    if (modal->modal) {
        pm_remove(modal->modal);
    }
    if (modal->shadow) {
        pm_remove(modal->shadow);
    }
    if (modal->prev_cursor_state != -1) {
        curs_set(modal->prev_cursor_state);
    }
    pm_update();
    free(modal);
}

static void draw_loading_modal(struct UiLoadingModal *m)
{
    if (!m || !m->modal) {
        return;
    }

    WINDOW *win = m->modal->win;
    int h = m->modal->h;
    int w = m->modal->w;

    werase(win);
    box(win, 0, 0);

    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, 1, 2, "%s", m->title);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    mvwhline(win, 2, 1, ACS_HLINE, w - 2);
    mvwaddch(win, 2, 0, ACS_LTEE);
    mvwaddch(win, 2, w - 1, ACS_RTEE);

    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, 4, 2, "%.*s", w - 4, m->message);
    wattroff(win, COLOR_PAIR(4));

    int bar_y = h - 3;
    int bar_x = 4;
    int bar_w = w - 8;
    if (bar_w < 10) {
        bar_w = (w > 6) ? w - 6 : w;
    }
    if (bar_w < 4) {
        bar_w = 4;
    }

    mvwaddch(win, bar_y, bar_x - 2, '[');
    mvwaddch(win, bar_y, bar_x + bar_w, ']');

    const char *empty_block = "\u2591";
    const char *full_block = "\u2588";

    wattron(win, COLOR_PAIR(3));
    wmove(win, bar_y, bar_x);
    for (int i = 0; i < bar_w; ++i) {
        waddstr(win, empty_block);
    }
    wattroff(win, COLOR_PAIR(3));

    if (m->indeterminate) {
        static const char spinner[] = "|/-\\";
        int idx = spinner[m->spinner_index % 4];
        wattron(win, COLOR_PAIR(5));
        mvwaddch(win, bar_y, bar_x + (bar_w / 2), idx);
        mvwprintw(win, bar_y + 1, bar_x - 2, "Working...");
        wattroff(win, COLOR_PAIR(5));
    } else {
        int filled = (int)(m->progress * bar_w + 0.5);
        if (filled < 0) filled = 0;
        if (filled > bar_w) filled = bar_w;
        if (filled > 0) {
            wattron(win, COLOR_PAIR(11));
            wmove(win, bar_y, bar_x);
            for (int i = 0; i < filled; ++i) {
                waddstr(win, full_block);
            }
            wattroff(win, COLOR_PAIR(11));
        }

        int percent = (int)(m->progress * 100.0 + 0.5);
        wattron(win, COLOR_PAIR(5));
        mvwprintw(win, bar_y + 1, bar_x - 2, "%3d%%", percent);
        wattroff(win, COLOR_PAIR(5));
    }

    pm_wnoutrefresh(m->shadow);
    pm_wnoutrefresh(m->modal);
    pm_update();
}
