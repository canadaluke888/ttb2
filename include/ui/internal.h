/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared internal UI state, layout data, and helper declarations. */

#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stddef.h>
#include <stdbool.h>
#include "data/table.h"
#include "data/table_view.h"
#include "core/settings.h"
#include "ui/ui.h"

/* Shared UI state exported across rendering, input, and dialog modules. */
extern int editing_mode;
extern int cursor_row;
extern int cursor_col;
extern int search_mode;
extern int search_hit_count;
extern int search_hit_index;
extern int search_sel_start;
extern int search_sel_len;
extern char search_query[128];
extern int col_page;
extern int cols_visible;
extern int total_pages;
extern int col_start;
extern int row_page;
extern int rows_visible;
extern int total_row_pages;
extern int low_ram_mode;
extern int row_gutter_enabled;
extern int footer_page;
extern int del_row_mode;
extern int del_col_mode;
extern TableView ui_table_view;

/* Active row or column reorder operation owned by the editor state. */
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

/* Generic dialog/menu completion result. */
typedef enum {
    UI_MENU_DONE = 0,
    UI_MENU_BACK = 1
} UiMenuResult;

/* Visible grid layout for the current render pass. */
typedef struct {
    int start_col;
    int end_col;
    int use_gutter;
    int gutter_width;
    long long row_number_base;
} UiGridLayout;

/* Formatting, validation, and core draw helpers. */
int ui_numeric_column_uses_sign_slot(const Table *table, int col);
int ui_numeric_text_width_for_grid(const char *text);
void apply_ui_color_settings(const AppSettings *settings);
bool validate_input(const char *input, DataType type);
void draw_ui(Table *table);
void draw_table_grid(Table *table);
void edit_header_cell(Table *table, int col);
void edit_body_cell(Table *table, int row, int col);
void prompt_clear_cell(Table *table, int row, int col);
void confirm_delete_row_at(Table *table, int row);
void confirm_delete_column_at(Table *table, int col);
int prompt_move_row_placement(Table *table, int source_row, int target_row);
int prompt_move_column_placement(Table *table, int source_col, int target_col);
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
int ui_pick_directory(char *out, size_t out_sz, const char *title);
int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          bool allow_empty);
int seek_mode_active(void);
int seek_mode_open_for_table(const char *db_path, const char *table_name, Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_first(Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_next(Table *view, int page_size, char *err, size_t err_sz);
int seek_mode_fetch_prev(Table *view, int page_size, char *err, size_t err_sz);
long long seek_mode_row_base(void);
int seek_mode_last_count(void);
void seek_mode_close(void);
void ui_reset_table_view(Table *table);
void ui_focus_location(Table *table, int actual_row, int col, int prefer_header);
int ui_visible_row_count(Table *table);
int ui_actual_row_for_visible(Table *table, int visible_row);
int ui_rebuild_table_view(Table *table, char *err, size_t err_sz);
int ui_table_view_is_active(void);
int ui_format_cell_value(const Table *table, int row, int col, char *buf, size_t buf_sz);
int ui_compute_column_widths(Table *table, int *col_widths);
int ui_alloc_column_widths(Table *table, int **col_widths_out);
void ui_update_column_paging(Table *table, const int *col_widths);
void ui_update_row_paging(Table *table);
void ui_fill_grid_layout(Table *table, UiGridLayout *layout);

/* Editing-mode helpers and cursor movement utilities. */
int ui_reorder_active(void);
void ui_clear_reorder_mode(void);
void ui_advance_footer_page(void);
void ui_enter_edit_mode(Table *table);
void ui_request_pending_grid_edit(void);
int ui_take_pending_grid_edit(void);

void ui_ensure_cursor_column_visible(const Table *table);
void ui_ensure_cursor_row_visible(Table *table);
void ui_move_cursor_left_paged(const Table *table);
void ui_move_cursor_right_paged(const Table *table);
void ui_move_cursor_up_paged(Table *table);
void ui_move_cursor_down_paged(Table *table);
void ui_move_cursor_left_cross_page(const Table *table);
void ui_move_cursor_right_cross_page(const Table *table);
void ui_move_cursor_up_cross_page(Table *table);
void ui_move_cursor_down_cross_page(Table *table);
void ui_clamp_cursor_viewport(const Table *table);
int ui_current_page_last_col(const Table *table);
int ui_current_page_last_row(Table *table);

/* Mouse and search-mode handlers. */
int ui_handle_cell_click(Table *table, int mouse_x, int mouse_y, int activate_editor);

void ui_search_enter(Table *table);
void ui_search_exit(void);
int ui_search_handle_key(Table *table, int ch);

int ui_handle_pending_grid_edit(Table *table);

/* Footer and cell text rendering helpers. */
void ui_draw_footer(Table *table);
void ui_draw_footer_box(void);
void ui_draw_table_title(const Table *table);
void ui_draw_view_status(Table *table);
void ui_draw_status_segment(int y, int *x, int max_x, int color_attr, const char *text);
void ui_draw_action_hint_segment(int y, int *x, int max_x, const char *text);
void ui_draw_footer_separator(int y, int *x, int max_x);
void ui_draw_footer_page_indicator(int y, int *x, int max_x);

void ui_add_repeat(const char *text, int count);
void ui_add_spaces(int count);
int ui_draw_cell_text(const char *text, int width, int color_pair);
int ui_draw_numeric_cell_text(const char *text, int width, int color_pair);
void ui_draw_highlighted_cell_text(const char *text, int width, int color_pair, int match_start, int match_len);
void ui_draw_cell_value(const Table *table, int col, const char *text, int width, int color_pair);

#endif
