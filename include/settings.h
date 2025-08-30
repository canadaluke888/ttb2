#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

typedef struct {
    bool autosave_enabled;
} AppSettings;

// Initialize defaults
void settings_init_defaults(AppSettings *s);

// Load/save settings from JSON file at path. Returns 0 on success.
int settings_load(const char *path, AppSettings *out);
int settings_save(const char *path, const AppSettings *s);

#endif

