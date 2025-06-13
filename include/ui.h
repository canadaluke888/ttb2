#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "tablecraft.h"

// Global UI state (defined in ui_loop.c)
extern int editing_mode;
extern int cursor_row;
extern int cursor_col;

// Initialization functions
void init_colors(void);
bool validate_input(const char *input, DataType type);

// Drawing functions
void draw_ui(Table *table);
void draw_table_grid(Table *table);

// Editing functions
void edit_header_cell(Table *table, int col);
void edit_body_cell(Table *table, int row, int col);

// Prompt functions
void prompt_add_column(Table *table);
void prompt_add_row(Table *table);
void show_table_menu(Table *table);
void show_save_format_menu(Table *table);
void prompt_rename_table(Table *table);

// UI loop function
void start_ui_loop(Table *table);

#endif // UI_H
