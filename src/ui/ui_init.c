#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"
#include "settings.h"

static int normalize_color(int color, int fallback)
{
    return (color >= 0 && color <= 7) ? color : fallback;
}

void apply_ui_color_settings(const AppSettings *settings)
{
    int table_name = 2;
    int hints = 3;
    int actions = 5;
    int lines = 4;

    if (settings) {
        table_name = normalize_color(settings->table_name_color, table_name);
        hints = normalize_color(settings->table_hint_color, hints);
        actions = normalize_color(settings->editor_actions_color, actions);
        lines = normalize_color(settings->table_line_color, lines);
    }

    init_pair(1, table_name, -1);    // Title
    init_pair(4, hints, -1);         // Column/row hints and selection prompts
    init_pair(5, actions, -1);       // Footer / editor action hotkeys
    init_pair(6, lines, -1);         // Unicode borders
}

void init_colors(void) {
    AppSettings settings;

    start_color();
    use_default_colors();  // Allow transparent backgrounds

    init_pair(2, COLOR_WHITE, -1);    // Table body
    init_pair(3, COLOR_CYAN, -1);       // Input box labels
    init_pair(10, COLOR_RED, -1);
    init_pair(11, COLOR_GREEN, -1);
    init_pair(12, COLOR_YELLOW, -1);
    init_pair(13, COLOR_BLUE, -1);
    init_pair(14, COLOR_MAGENTA, -1);
    init_pair(15, COLOR_CYAN, -1);
    init_pair(16, COLOR_WHITE, -1);

    settings_init_defaults(&settings);
    settings_load(settings_default_path(), &settings);
    apply_ui_color_settings(&settings);
}

bool validate_input(const char *input, DataType type) {
    if (!input || strlen(input) == 0)
        return false;

    char *endptr;
    switch (type) {
        case TYPE_INT:
            strtol(input, &endptr, 10);
            return *endptr == '\0';
        case TYPE_FLOAT:
            strtof(input, &endptr);
            return *endptr == '\0';
        case TYPE_BOOL:
            return (strcasecmp(input, "true") == 0 ||
                    strcasecmp(input, "false") == 0 ||
                    strcmp(input, "1") == 0 ||
                    strcmp(input, "0") == 0);
        case TYPE_STR:
            return true;
        default:
            return false;
    }
}
