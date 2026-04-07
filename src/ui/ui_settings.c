#include <ncurses.h>
#include <string.h>
#include <stdbool.h>
#include "panel_manager.h"
#include "workspace.h"
#include "errors.h"
#include "settings.h"
#include "ui.h"

static AppSettings g_settings;
static int g_loaded = 0;

static void ensure_loaded(void) {
    if (g_loaded) return;
    settings_init_defaults(&g_settings);
    settings_ensure_directory();
    settings_load(settings_default_path(), &g_settings);
    workspace_set_autosave_enabled(g_settings.autosave_enabled);
    low_ram_mode = g_settings.low_ram_enabled ? 1 : 0;
    row_gutter_enabled = g_settings.show_row_gutter ? 1 : 0;
    g_loaded = 1;
}

UiMenuResult show_settings_menu(void) {
    ensure_loaded();
    noecho(); curs_set(0);
    const char *labels[] = {
        "Autosave workspace: ",
        "Type inference: ",
        "Low-RAM seek paging: ",
        "Row gutter: ",
        "Save & Close",
        "Back"
    };
    int count = (int)(sizeof(labels) / sizeof(labels[0]));
    int sel = 0; int ch;
    UiMenuResult result = UI_MENU_BACK;

    int h = count + 5; /* title row + underline + options + padding */
    if (h < 12) h = 12;
    if (h > LINES - 2) h = LINES - 2;
    int w = COLS - 6;
    if (w < 30) w = COLS - 2;
    if (w < 24) w = 24;
    int y = (LINES - h) / 2; int x = (COLS - w) / 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);
    while (1) {
        werase(modal->win); box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Settings");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, 2, 0, ACS_LTEE);
        mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
        int inner_w = w - 4; // 2-char margins inside border
        for (int i = 0; i < count; ++i) {
            if (i == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            char linebuf[256]; linebuf[0] = '\0';
            if (i == 0) snprintf(linebuf, sizeof(linebuf), "%s%s", labels[i], g_settings.autosave_enabled ? "On" : "Off");
            else if (i == 1) snprintf(linebuf, sizeof(linebuf), "%s%s", labels[i], g_settings.type_infer_enabled ? "On" : "Off");
            else if (i == 2) snprintf(linebuf, sizeof(linebuf), "%s%s", labels[i], g_settings.low_ram_enabled ? "On" : "Off");
            else if (i == 3) snprintf(linebuf, sizeof(linebuf), "%s%s", labels[i], g_settings.show_row_gutter ? "On" : "Off");
            else snprintf(linebuf, sizeof(linebuf), "%s", labels[i]);
            // clear line region and print clipped
            int row = 3 + i;
            mvwchgat(modal->win, row, 1, w - 2, A_NORMAL, 0, NULL);
            mvwprintw(modal->win, row, 2, "%.*s", inner_w, linebuf);
            if (i == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) sel = (sel > 0) ? sel - 1 : count - 1;
        else if (ch == KEY_DOWN) sel = (sel + 1) % count;
        else if (ch == '\n') {
            if (sel == 0) { g_settings.autosave_enabled = !g_settings.autosave_enabled; workspace_set_autosave_enabled(g_settings.autosave_enabled); }
            else if (sel == 1) { g_settings.type_infer_enabled = !g_settings.type_infer_enabled; }
            else if (sel == 2) { g_settings.low_ram_enabled = !g_settings.low_ram_enabled; low_ram_mode = g_settings.low_ram_enabled ? 1 : 0; }
            else if (sel == 3) { g_settings.show_row_gutter = !g_settings.show_row_gutter; row_gutter_enabled = g_settings.show_row_gutter ? 1 : 0; }
            else if (sel == 4) { settings_save(settings_default_path(), &g_settings); result = UI_MENU_DONE; break; }
            else if (sel == 5) { result = UI_MENU_BACK; break; }
        } else if (ch == 27) { result = UI_MENU_DONE; break; }
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
    return result;
}
