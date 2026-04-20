/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Panel stack implementation for layered ncurses windows. */

#include "ui/panel_manager.h"
#include <stdlib.h>
#include <string.h>

#define PM_MAX 128

static PmNode *PM_LIST[PM_MAX];
static int PM_COUNT = 0;

/* Order panel nodes by z-index so higher layers restack correctly. */
static int cmp_nodes(const void *a, const void *b) {
    const PmNode *na = *(PmNode * const *)a;
    const PmNode *nb = *(PmNode * const *)b;
    if (na->z_index < nb->z_index) return -1;
    if (na->z_index > nb->z_index) return  1;
    return (na < nb) ? -1 : (na > nb);
}

void pm_init(void) {}
/* Tear down every tracked panel-managed window in z-order storage. */
void pm_teardown(void) {
    for (int i = 0; i < PM_COUNT; ++i) {
        if (!PM_LIST[i]) continue;
        del_panel(PM_LIST[i]->panel);
        delwin(PM_LIST[i]->win);
        free(PM_LIST[i]);
        PM_LIST[i] = NULL;
    }
    PM_COUNT = 0;
}

/* Restack tracked panels so their z-order matches the stored role order. */
static void restack(void) {
    qsort(PM_LIST, PM_COUNT, sizeof(PmNode*), cmp_nodes);
    for (int i = 0; i < PM_COUNT; ++i) {
        top_panel(PM_LIST[i]->panel);
    }
}

PmNode *pm_add(int y, int x, int h, int w, int z_index, PmLayerRole role) {
    if (PM_COUNT >= PM_MAX) return NULL;
    WINDOW *win = newwin(h, w, y, x);
    if (!win) return NULL;
    PANEL *pan = new_panel(win);
    if (!pan) { delwin(win); return NULL; }

    PmNode *n = (PmNode*)calloc(1, sizeof(PmNode));
    n->panel = pan; n->win = win; n->z_index = z_index; n->role = role;
    n->y = y; n->x = x; n->h = h; n->w = w; n->visible = true;

    PM_LIST[PM_COUNT++] = n;
    restack();
    return n;
}

/* Remove one tracked panel-managed window from the stack and free it. */
void pm_remove(PmNode *n) {
    if (!n) return;
    int k = -1;
    for (int i = 0; i < PM_COUNT; ++i) if (PM_LIST[i] == n) { k = i; break; }
    if (k >= 0) {
        for (int i = k; i < PM_COUNT-1; ++i) PM_LIST[i] = PM_LIST[i+1];
        PM_COUNT--;
    }
    del_panel(n->panel);
    delwin(n->win);
    free(n);
    restack();
}

void pm_show(PmNode *n) { if (!n) return; show_panel(n->panel); n->visible = true; }
void pm_hide(PmNode *n) { if (!n) return; hide_panel(n->panel); n->visible = false; }

void pm_set_z(PmNode *n, int z_index) { if (!n) return; n->z_index = z_index; restack(); }
void pm_set_role(PmNode *n, PmLayerRole role) { if (!n) return; n->role = role; }

/* Move a tracked panel-managed window to new screen coordinates. */
void pm_move(PmNode *n, int y, int x) {
    if (!n) return;
    n->y = y; n->x = x;
    move_panel(n->panel, y, x);
}

/* Resize a tracked panel-managed window and refresh its panel binding. */
void pm_resize(PmNode *n, int h, int w) {
    if (!n) return;
    n->h = h; n->w = w;
    wresize(n->win, h, w);
    replace_panel(n->panel, n->win);
}

void pm_wnoutrefresh(PmNode *n) { if (!n) return; wnoutrefresh(n->win); }
void pm_update(void) { restack(); update_panels(); doupdate(); }

/* Recenter a tracked window within the current terminal size. */
void pm_center(PmNode *n) {
    if (!n) return;
    int H, W; getmaxyx(stdscr, H, W);
    int y = (H - n->h) / 2;
    int x = (W - n->w) / 2;
    pm_move(n, y, x);
}

void pm_on_resize(void) { restack(); }
