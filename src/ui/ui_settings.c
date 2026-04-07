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
    apply_ui_color_settings(&g_settings);
    g_loaded = 1;
}

static const char *color_name(int color)
{
    switch (color) {
        case COLOR_BLACK: return "Black";
        case COLOR_RED: return "Red";
        case COLOR_GREEN: return "Green";
        case COLOR_YELLOW: return "Yellow";
        case COLOR_BLUE: return "Blue";
        case COLOR_MAGENTA: return "Magenta";
        case COLOR_CYAN: return "Cyan";
        case COLOR_WHITE: return "White";
        default: return "Default";
    }
}

static int next_color(int color)
{
    if (color < 0 || color >= 7) return 0;
    return color + 1;
}

static int is_selectable_row(int row)
{
    return row != 0 && row != 4;
}

static int next_selectable_row(int row, int dir, int count)
{
    if (count <= 0) return 0;
    do {
        row = (row + dir + count) % count;
    } while (!is_selectable_row(row));
    return row;
}

UiMenuResult show_settings_menu(void) {
    ensure_loaded();
    noecho(); curs_set(0);
    enum {
        ROW_CORE = 0,
        ROW_AUTOSAVE,
        ROW_TYPE_INFER,
        ROW_LOW_RAM,
        ROW_COSMETIC,
        ROW_ROW_GUTTER,
        ROW_ACTION_COLOR,
        ROW_LINE_COLOR,
        ROW_NAME_COLOR,
        ROW_HINT_COLOR,
        ROW_SAVE,
        ROW_BACK,
        ROW_COUNT
    };
    int count = ROW_COUNT;
    int sel = ROW_AUTOSAVE; int ch;
    UiMenuResult result = UI_MENU_BACK;

    int h = count + 5; /* title row + underline + options + padding */
    if (h < 16) h = 16;
    if (h > LINES - 2) h = LINES - 2;
    int w = COLS - 6;
    if (w < 44) w = COLS - 2;
    if (w < 32) w = 32;
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
            char linebuf[256]; linebuf[0] = '\0';
            if (i == ROW_CORE) snprintf(linebuf, sizeof(linebuf), "General");
            else if (i == ROW_AUTOSAVE) snprintf(linebuf, sizeof(linebuf), "Autosave workspace: %s", g_settings.autosave_enabled ? "On" : "Off");
            else if (i == ROW_TYPE_INFER) snprintf(linebuf, sizeof(linebuf), "Type inference: %s", g_settings.type_infer_enabled ? "On" : "Off");
            else if (i == ROW_LOW_RAM) snprintf(linebuf, sizeof(linebuf), "Low-RAM seek paging: %s", g_settings.low_ram_enabled ? "On" : "Off");
            else if (i == ROW_COSMETIC) snprintf(linebuf, sizeof(linebuf), "Appearance");
            else if (i == ROW_ROW_GUTTER) snprintf(linebuf, sizeof(linebuf), "Row gutter: %s", g_settings.show_row_gutter ? "On" : "Off");
            else if (i == ROW_ACTION_COLOR) snprintf(linebuf, sizeof(linebuf), "Editor actions color: %s", color_name(g_settings.editor_actions_color));
            else if (i == ROW_LINE_COLOR) snprintf(linebuf, sizeof(linebuf), "Table line color: %s", color_name(g_settings.table_line_color));
            else if (i == ROW_NAME_COLOR) snprintf(linebuf, sizeof(linebuf), "Table name color: %s", color_name(g_settings.table_name_color));
            else if (i == ROW_HINT_COLOR) snprintf(linebuf, sizeof(linebuf), "Column/row hints color: %s", color_name(g_settings.table_hint_color));
            else if (i == ROW_SAVE) snprintf(linebuf, sizeof(linebuf), "Save & Close");
            else if (i == ROW_BACK) snprintf(linebuf, sizeof(linebuf), "Back");
            // clear line region and print clipped
            int row = 3 + i;
            mvwchgat(modal->win, row, 1, w - 2, A_NORMAL, 0, NULL);
            if (i == ROW_COSMETIC) {
                mvwhline(modal->win, row - 1, 1, ACS_HLINE, w - 2);
                mvwaddch(modal->win, row - 1, 0, ACS_LTEE);
                mvwaddch(modal->win, row - 1, w - 1, ACS_RTEE);
            }
            if (i == ROW_CORE || i == ROW_COSMETIC) wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            else if (i == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, row, 2, "%.*s", inner_w, linebuf);
            if (i == ROW_CORE || i == ROW_COSMETIC) wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            else if (i == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_UP) sel = next_selectable_row(sel, -1, count);
        else if (ch == KEY_DOWN) sel = next_selectable_row(sel, 1, count);
        else if (ch == '\n') {
            if (sel == ROW_AUTOSAVE) { g_settings.autosave_enabled = !g_settings.autosave_enabled; workspace_set_autosave_enabled(g_settings.autosave_enabled); }
            else if (sel == ROW_TYPE_INFER) { g_settings.type_infer_enabled = !g_settings.type_infer_enabled; }
            else if (sel == ROW_LOW_RAM) { g_settings.low_ram_enabled = !g_settings.low_ram_enabled; low_ram_mode = g_settings.low_ram_enabled ? 1 : 0; }
            else if (sel == ROW_ROW_GUTTER) { g_settings.show_row_gutter = !g_settings.show_row_gutter; row_gutter_enabled = g_settings.show_row_gutter ? 1 : 0; }
            else if (sel == ROW_ACTION_COLOR) { g_settings.editor_actions_color = next_color(g_settings.editor_actions_color); apply_ui_color_settings(&g_settings); }
            else if (sel == ROW_LINE_COLOR) { g_settings.table_line_color = next_color(g_settings.table_line_color); apply_ui_color_settings(&g_settings); }
            else if (sel == ROW_NAME_COLOR) { g_settings.table_name_color = next_color(g_settings.table_name_color); apply_ui_color_settings(&g_settings); }
            else if (sel == ROW_HINT_COLOR) { g_settings.table_hint_color = next_color(g_settings.table_hint_color); apply_ui_color_settings(&g_settings); }
            else if (sel == ROW_SAVE) { settings_save(settings_default_path(), &g_settings); result = UI_MENU_DONE; break; }
            else if (sel == ROW_BACK) { result = UI_MENU_BACK; break; }
        } else if (ch == 27) { result = UI_MENU_DONE; break; }
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
    return result;
}
