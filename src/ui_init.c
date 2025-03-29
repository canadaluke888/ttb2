#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "tablecraft.h"
#include "ui.h"

void init_colors(void) {
    start_color();
    use_default_colors();  // Allow transparent backgrounds

    init_pair(1, COLOR_GREEN, -1);    // Title
    init_pair(2, COLOR_WHITE, -1);    // Table body
    init_pair(3, COLOR_CYAN, -1);       // Input box labels
    init_pair(4, COLOR_YELLOW, -1);     // Input prompts
    init_pair(5, COLOR_MAGENTA, -1);    // Footer / hotkeys
    init_pair(6, COLOR_BLUE, -1);       // Unicode borders
    init_pair(10, COLOR_RED, -1);
    init_pair(11, COLOR_GREEN, -1);
    init_pair(12, COLOR_YELLOW, -1);
    init_pair(13, COLOR_BLUE, -1);
    init_pair(14, COLOR_MAGENTA, -1);
    init_pair(15, COLOR_CYAN, -1);
    init_pair(16, COLOR_WHITE, -1);
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
