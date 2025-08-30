#include <ncurses.h>
#include <string.h>
#include <stdbool.h>
#include "panel_manager.h"
#include "db_manager.h"
#include "errors.h"
#include "settings.h"

static AppSettings g_settings;
static int g_loaded = 0;
static const char *SETTINGS_PATH = "settings.json";

static void ensure_loaded(void) {
    if (g_loaded) return;
    settings_init_defaults(&g_settings);
    settings_load(SETTINGS_PATH, &g_settings);
    db_set_autosave_enabled(g_settings.autosave_enabled);
    g_loaded = 1;
}

void show_settings_menu(void) {
    ensure_loaded();
    noecho(); curs_set(0);
    const char *labels[] = {"Autosave: ", "Save & Close", "Cancel"};
    int count = 3;
    int sel = 0; int ch;
    int h = 7; int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);
    while (1) {
        werase(modal->win); box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Settings");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        for (int i = 0; i < count; ++i) {
            if (i == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            if (i == 0) {
                mvwprintw(modal->win, 2 + i, 2, "%s%s", labels[i], g_settings.autosave_enabled ? "On" : "Off");
            } else {
                mvwprintw(modal->win, 2 + i, 2, "%s", labels[i]);
            }
            if (i == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) sel = (sel > 0) ? sel - 1 : count - 1;
        else if (ch == KEY_DOWN) sel = (sel + 1) % count;
        else if (ch == '\n') {
            if (sel == 0) { g_settings.autosave_enabled = !g_settings.autosave_enabled; db_set_autosave_enabled(g_settings.autosave_enabled); }
            else if (sel == 1) { settings_save(SETTINGS_PATH, &g_settings); break; }
            else if (sel == 2) { break; }
        } else if (ch == 27) { break; }
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
}

