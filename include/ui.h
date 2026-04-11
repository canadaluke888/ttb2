#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "table.h"
#include "table_view.h"
#include "settings.h"

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
extern int footer_page;
// Destructive selection modes inside edit mode
extern int del_row_mode;  // when 1, highlight full row and navigate with ↑/↓, Enter confirms delete
extern int del_col_mode;  // when 1, highlight full column and navigate with ←/→, Enter confirms delete
extern TableView ui_table_view;

typedef enum {
    UI_REORDER_NONE = 0,
    UI_REORDER_MOVE_ROW,
    UI_REORDER_MOVE_COL,
    UI_REORDER_SWAP_ROW,
    UI_REORDER_SWAP_COL
} UiReorderMode;

extern UiReorderMode reorder_mode;
extern int reorder_source_row;
extern int reorder_source_col;

typedef enum {
    UI_MENU_DONE = 0,
    UI_MENU_BACK = 1
} UiMenuResult;

// Initialization functions
void init_colors(void);
void apply_ui_color_settings(const AppSettings *settings);
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
int prompt_move_row_placement(Table *table, int source_row, int target_row);
int prompt_move_column_placement(Table *table, int source_col, int target_col);

// Prompt functions
void prompt_add_column(Table *table);
void prompt_add_row(Table *table);
int prompt_insert_column_at(Table *table, int col_index);
int prompt_insert_row_at(Table *table, int row_index);
void show_table_menu(Table *table);
void prompt_sort_rows(Table *table);
void prompt_filter_rows(Table *table);
void clear_table_view_prompt(Table *table);
UiMenuResult show_export_menu(Table *table);
void prompt_rename_table(Table *table);
UiMenuResult show_settings_menu(void);
UiMenuResult show_open_file(Table *table);
int ui_open_path(Table *table, const char *path, int preserve_current_table, int show_book_success);
int ui_pick_directory(char *out, size_t out_sz, const char *title);
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
void ui_reset_table_view(Table *table);
int ui_visible_row_count(Table *table);
int ui_actual_row_for_visible(Table *table, int visible_row);
int ui_rebuild_table_view(Table *table, char *err, size_t err_sz);
int ui_table_view_is_active(void);
int ui_format_cell_value(const Table *table, int row, int col, char *buf, size_t buf_sz);

#endif // UI_H
