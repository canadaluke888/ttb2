/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Settings dialog logic for runtime toggles and themes. */

#include <ncurses.h>
#include <stdbool.h>
#include "core/workspace.h"
#include "core/settings.h"
#include "ui/internal.h"
#include "ui/dialog_internal.h"

static AppSettings g_settings;
static int g_loaded = 0;

/* Lazily load settings once and apply their runtime side effects. */
static void ensure_loaded(void) {
    if (g_loaded) return;
    settings_init_defaults(&g_settings);
    settings_ensure_directory();
    settings_load(settings_default_path(), &g_settings);
    workspace_set_autosave_enabled(g_settings.autosave_enabled);
    ui_set_row_gutter_enabled(g_settings.show_row_gutter);
    settings_set_row_vectorization_enabled(g_settings.row_vectorization_enabled);
    apply_ui_color_settings(&g_settings);
    g_loaded = 1;
}

/* Show the settings dialog for runtime toggles and theme selection. */
UiMenuResult show_settings_menu(void) {
    enum {
        SETTINGS_ACTION_AUTOSAVE = 1,
        SETTINGS_ACTION_TYPE_INFER,
        SETTINGS_ACTION_ROW_VECTORIZATION,
        SETTINGS_ACTION_ROW_GUTTER,
        SETTINGS_ACTION_THEME,
        SETTINGS_ACTION_SAVE,
        SETTINGS_ACTION_BACK
    };
    UiMenuResult result = UI_MENU_BACK;

    ensure_loaded();
    while (1) {
        char autosave[64];
        char type_infer[64];
        char row_vectorization[64];
        char row_gutter[64];
        char theme[96];
        const UiDialogListRow rows[] = {
            {UI_DIALOG_LIST_ROW_HEADER, "General", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_SPACER, "", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_ITEM, autosave, SETTINGS_ACTION_AUTOSAVE, 1, 0},
            {UI_DIALOG_LIST_ROW_ITEM, type_infer, SETTINGS_ACTION_TYPE_INFER, 1, 0},
            {UI_DIALOG_LIST_ROW_ITEM, row_vectorization, SETTINGS_ACTION_ROW_VECTORIZATION, 1, 0},
            {UI_DIALOG_LIST_ROW_DIVIDER, "", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_HEADER, "Appearance", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_SPACER, "", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_ITEM, row_gutter, SETTINGS_ACTION_ROW_GUTTER, 1, 0},
            {UI_DIALOG_LIST_ROW_ITEM, theme, SETTINGS_ACTION_THEME, 1, 0},
            {UI_DIALOG_LIST_ROW_DIVIDER, "", -1, 0, 0},
            {UI_DIALOG_LIST_ROW_ITEM, "Save & Close", SETTINGS_ACTION_SAVE, 1, 0},
            {UI_DIALOG_LIST_ROW_ITEM, "Back", SETTINGS_ACTION_BACK, 1, 0}
        };
        const UiDialogListOptions options = {
            "Settings",
            NULL,
            44,
            0,
            0,
            0,
            true,
            true,
            SETTINGS_ACTION_AUTOSAVE
        };
        int picked;

        snprintf(autosave, sizeof(autosave), "Autosave workspace: %s", g_settings.autosave_enabled ? "On" : "Off");
        snprintf(type_infer, sizeof(type_infer), "Type inference: %s", g_settings.type_infer_enabled ? "On" : "Off");
        snprintf(row_vectorization, sizeof(row_vectorization), "Row vectorization: %s", g_settings.row_vectorization_enabled ? "On" : "Off");
        snprintf(row_gutter, sizeof(row_gutter), "Row gutter: %s", g_settings.show_row_gutter ? "On" : "Off");
        snprintf(theme, sizeof(theme), "Theme: %s", settings_theme_name(g_settings.theme_id));

        noecho();
        curs_set(0);
        picked = ui_dialog_show_styled_list_modal(&options, rows, (int)(sizeof(rows) / sizeof(rows[0])));
        if (picked == SETTINGS_ACTION_AUTOSAVE) {
            g_settings.autosave_enabled = !g_settings.autosave_enabled;
            workspace_set_autosave_enabled(g_settings.autosave_enabled);
        } else if (picked == SETTINGS_ACTION_TYPE_INFER) {
            g_settings.type_infer_enabled = !g_settings.type_infer_enabled;
        } else if (picked == SETTINGS_ACTION_ROW_VECTORIZATION) {
            g_settings.row_vectorization_enabled = !g_settings.row_vectorization_enabled;
            settings_set_row_vectorization_enabled(g_settings.row_vectorization_enabled);
        } else if (picked == SETTINGS_ACTION_ROW_GUTTER) {
            g_settings.show_row_gutter = !g_settings.show_row_gutter;
            ui_set_row_gutter_enabled(g_settings.show_row_gutter);
        } else if (picked == SETTINGS_ACTION_THEME) {
            g_settings.theme_id = (g_settings.theme_id + 1) % settings_theme_count();
            apply_ui_color_settings(&g_settings);
        } else if (picked == SETTINGS_ACTION_SAVE) {
            settings_save(settings_default_path(), &g_settings);
            result = UI_MENU_DONE;
            break;
        } else {
            result = (picked < 0) ? UI_MENU_DONE : UI_MENU_BACK;
            break;
        }
    }
    return result;
}
