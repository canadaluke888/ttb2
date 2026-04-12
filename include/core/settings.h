/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Persistent application settings and theme palette interfaces. */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

/* Theme palette IDs consumed by the ncurses rendering layer. */
typedef struct {
    int table_name_color;
    int table_hint_color;
    int editor_actions_color;
    int table_line_color;
    int key_hint_color;
    int separator_color;
} AppThemePalette;

/* Persistent application settings loaded from the JSON settings file. */
typedef struct {
    bool autosave_enabled;
    bool type_infer_enabled;
    bool low_ram_enabled;   // use seek-only paging for large tables
    bool show_row_gutter;   // show row number gutter in grid
    int theme_id;
} AppSettings;

/* Reset settings to their built-in defaults. */
void settings_init_defaults(AppSettings *s);

/* Load or save the JSON settings file at the given path. */
int settings_load(const char *path, AppSettings *out);
int settings_save(const char *path, const AppSettings *s);
/* Ensure the settings directory exists before reading or writing files. */
int settings_ensure_directory(void);
/* Theme metadata helpers used by the settings UI and renderer. */
const char *settings_default_path(void);
int settings_theme_count(void);
int settings_normalize_theme(int theme_id);
const char *settings_theme_name(int theme_id);
void settings_theme_palette(int theme_id, AppThemePalette *out);

#endif
