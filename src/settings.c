/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Settings persistence, defaults, and theme palette lookups. */

#include "core/settings.h"
#include <json-c/json.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define SETTINGS_DIR "settings"
#define SETTINGS_FILE SETTINGS_DIR "/settings.json"

static int path_is_directory(const char *path)
{
    struct stat st;

    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int normalize_color(int color, int fallback)
{
    return (color >= 0 && color <= 7) ? color : fallback;
}

static const struct {
    const char *name;
    AppThemePalette palette;
} THEMES[] = {
    {"Classic", {2, 3, 5, 4, 7, 6}},
    {"Ocean",   {6, 4, 7, 6, 3, 5}},
    {"Forest",  {2, 3, 6, 2, 7, 3}},
    {"Sunset",  {3, 5, 7, 1, 6, 3}}
};

const char *settings_default_path(void)
{
    return SETTINGS_FILE;
}

int settings_ensure_directory(void)
{
    if (mkdir(SETTINGS_DIR, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST && path_is_directory(SETTINGS_DIR)) {
        return 0;
    }
    return -1;
}

/* Seed the settings structure with the built-in defaults. */
void settings_init_defaults(AppSettings *s) {
    if (!s) return;
    s->autosave_enabled = true;
    s->type_infer_enabled = true;
    s->show_row_gutter = true;
    s->theme_id = 0;
}

/* Load settings from disk, falling back to defaults on missing values. */
int settings_load(const char *path, AppSettings *out) {
    if (!out) return -1;
    settings_init_defaults(out);
    if (!path || !path[0]) path = settings_default_path();

    struct json_object *root = json_object_from_file(path);
    if (!root) return -1;
    struct json_object *jauto = NULL;
    if (json_object_object_get_ex(root, "autosave_enabled", &jauto)) {
        out->autosave_enabled = json_object_get_boolean(jauto);
    }
    struct json_object *jinf = NULL;
    if (json_object_object_get_ex(root, "type_infer_enabled", &jinf)) {
        out->type_infer_enabled = json_object_get_boolean(jinf);
    }
    struct json_object *jg = NULL;
    if (json_object_object_get_ex(root, "show_row_gutter", &jg)) {
        out->show_row_gutter = json_object_get_boolean(jg);
    }
    struct json_object *jtheme = NULL;
    if (json_object_object_get_ex(root, "theme_id", &jtheme)) {
        out->theme_id = settings_normalize_theme(json_object_get_int(jtheme));
    }
    json_object_put(root);
    return 0;
}

/* Write the current settings structure to the JSON settings file. */
int settings_save(const char *path, const AppSettings *s) {
    if (!s) return -1;
    if (!path || !path[0]) path = settings_default_path();
    if (settings_ensure_directory() != 0) {
        return -1;
    }
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "autosave_enabled", json_object_new_boolean(s->autosave_enabled));
    json_object_object_add(root, "type_infer_enabled", json_object_new_boolean(s->type_infer_enabled));
    json_object_object_add(root, "show_row_gutter", json_object_new_boolean(s->show_row_gutter));
    json_object_object_add(root, "theme_id", json_object_new_int(settings_normalize_theme(s->theme_id)));
    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    json_object_put(root);
    return (rc == 0) ? 0 : -1;
}

int settings_theme_count(void)
{
    return (int)(sizeof(THEMES) / sizeof(THEMES[0]));
}

int settings_normalize_theme(int theme_id)
{
    return (theme_id >= 0 && theme_id < settings_theme_count()) ? theme_id : 0;
}

const char *settings_theme_name(int theme_id)
{
    return THEMES[settings_normalize_theme(theme_id)].name;
}

void settings_theme_palette(int theme_id, AppThemePalette *out)
{
    int normalized = settings_normalize_theme(theme_id);

    if (!out) return;
    *out = THEMES[normalized].palette;
    out->table_name_color = normalize_color(out->table_name_color, 2);
    out->table_hint_color = normalize_color(out->table_hint_color, 3);
    out->editor_actions_color = normalize_color(out->editor_actions_color, 5);
    out->table_line_color = normalize_color(out->table_line_color, 4);
    out->key_hint_color = normalize_color(out->key_hint_color, 7);
    out->separator_color = normalize_color(out->separator_color, 6);
}
