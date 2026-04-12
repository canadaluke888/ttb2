#pragma once
#include <ncurses.h>
#include <panel.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_LAYER_MAIN     = 10,
    PM_LAYER_SIDEBAR  = 20,
    PM_LAYER_FOOTER   = 30,
    PM_LAYER_TOAST    = 900,
    PM_LAYER_MODAL_SHADOW = 995,
    PM_LAYER_MODAL    = 1000
} PmLayerRole;

typedef struct {
    PANEL   *panel;
    WINDOW  *win;        // owned by us
    int      z_index;    // higher draws on top
    PmLayerRole role;
    int      y, x, h, w; // last known geometry
    bool     visible;
} PmNode;

void pm_init(void);                       // call after initscr()
void pm_teardown(void);                   // deletes all panels/windows

PmNode *pm_add(int y, int x, int h, int w, int z_index, PmLayerRole role);
void    pm_remove(PmNode *n);

void    pm_show(PmNode *n);
void    pm_hide(PmNode *n);
void    pm_set_z(PmNode *n, int z_index);
void    pm_set_role(PmNode *n, PmLayerRole role);
void    pm_move(PmNode *n, int y, int x);
void    pm_resize(PmNode *n, int h, int w);

void    pm_wnoutrefresh(PmNode *n);
void    pm_update(void);
void    pm_center(PmNode *n);
void    pm_on_resize(void);

#ifdef __cplusplus
}
#endif

