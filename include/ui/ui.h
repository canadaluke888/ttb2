#ifndef UI_H
#define UI_H

#include "data/table.h"
void init_colors(void);
int ui_open_path(Table *table, const char *path, int preserve_current_table, int show_book_success);
void start_ui_loop(Table *table);
void ui_set_low_ram_mode(int enabled);
int ui_low_ram_mode_enabled(void);
void ui_set_row_gutter_enabled(int enabled);
int ui_row_gutter_enabled(void);

#endif
