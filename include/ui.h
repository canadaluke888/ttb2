#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "tablecraft.h"

// Global UI state (defined in ui_loop.c)
extern int editing_mode;
extern int cursor_row;
extern int cursor_col;
extern int search_mode; // 1 when search navigation is active
extern int search_hit_count;   // total number of matches
extern int search_hit_index;   // current match index (0-based)
extern int col_page; // column paging start index
extern int cols_visible; // number of visible columns on current page
extern int total_pages;  // total column pages
extern int col_start;    // current visible column start index (computed)
// Row paging state
extern int row_page;      // row paging start page index
extern int rows_visible;  // number of visible rows on current page
extern int total_row_pages; // total row pages

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
void show_db_manager(Table *table);
void show_settings_menu(void);
void show_open_file(Table *table);

// UI loop function
void start_ui_loop(Table *table);

#endif // UI_H
