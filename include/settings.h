#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

typedef struct {
    bool autosave_enabled;
    bool type_infer_enabled;
    bool low_ram_enabled;   // use seek-only paging for large tables
    bool show_row_gutter;   // show row number gutter in grid
    int editor_actions_color;
    int table_line_color;
    int table_name_color;
    int table_hint_color;
} AppSettings;

// Initialize defaults
void settings_init_defaults(AppSettings *s);

// Load/save settings from JSON file at path. Returns 0 on success.
int settings_load(const char *path, AppSettings *out);
int settings_save(const char *path, const AppSettings *s);
int settings_ensure_directory(void);
const char *settings_default_path(void);

#endif
