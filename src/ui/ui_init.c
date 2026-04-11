#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "table.h"
#include "ui.h"
#include "settings.h"

void apply_ui_color_settings(const AppSettings *settings)
{
    AppThemePalette palette;

    settings_theme_palette(settings ? settings->theme_id : 0, &palette);

    init_pair(1, palette.table_name_color, -1);    // Title
    init_pair(4, palette.table_hint_color, -1);    // Column/row hints and selection prompts
    init_pair(5, palette.editor_actions_color, -1); // Footer / editor action hotkeys
    init_pair(6, palette.table_line_color, -1);    // Unicode borders
    init_pair(7, palette.key_hint_color, -1);      // Key hints like [Enter]
    init_pair(8, palette.separator_color, -1);     // Footer separators
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
