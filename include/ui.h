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
extern int search_sel_start;   // start index of current match within cell
extern int search_sel_len;     // length of current match
extern char search_query[128]; // current search text (for highlighting)
extern int col_page; // column paging start index
extern int cols_visible; // number of visible columns on current page
extern int total_pages;  // total column pages
extern int col_start;    // current visible column start index (computed)
// Row paging state
extern int row_page;      // row paging start page index
extern int rows_visible;  // number of visible rows on current page
extern int total_row_pages; // total row pages
// Performance mode
extern int low_ram_mode; // when 1, UI fetches windows via seekdb
extern int row_gutter_enabled; // show/hide row number gutter
// Destructive selection modes inside edit mode
extern int del_row_mode;  // when 1, highlight full row and navigate with ↑/↓, Enter confirms delete
extern int del_col_mode;  // when 1, highlight full column and navigate with ←/→, Enter confirms delete

// Initialization functions
void init_colors(void);
bool validate_input(const char *input, DataType type);

// Drawing functions
void draw_ui(Table *table);
void draw_table_grid(Table *table);

// Editing functions
void edit_header_cell(Table *table, int col);
void edit_body_cell(Table *table, int row, int col);
// Edit-mode destructive actions
void prompt_clear_cell(Table *table, int row, int col);
void confirm_delete_row_at(Table *table, int row);
void confirm_delete_column_at(Table *table, int col);

// Prompt functions
void prompt_add_column(Table *table);
void prompt_add_row(Table *table);
void show_table_menu(Table *table);
void show_export_menu(Table *table);
void prompt_rename_table(Table *table);
void show_db_manager(Table *table);
void show_settings_menu(void);
void show_open_file(Table *table);
int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty);

// Seek-mode helpers (for low-RAM browsing)
int seek_mode_active(void);
int seek_mode_open_for_table(const char *db_path, const char *table_name, Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_first(Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_next(Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_prev(Table *view, int page_size, char *err, size_t err_sz);
long long seek_mode_row_base(void);
int seek_mode_last_count(void);
void seek_mode_close(void);

// UI loop function
void start_ui_loop(Table *table);

#endif // UI_H
