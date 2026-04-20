/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared dialog helpers used by modal UI flows. */

#ifndef UI_DIALOG_INTERNAL_H
#define UI_DIALOG_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    UI_DIALOG_LIST_ROW_HEADER = 0,
    UI_DIALOG_LIST_ROW_DIVIDER,
    UI_DIALOG_LIST_ROW_SPACER,
    UI_DIALOG_LIST_ROW_ITEM
} UiDialogListRowKind;

typedef struct {
    UiDialogListRowKind kind;
    const char *label;
    int id;
    int enabled;
    int indent;
} UiDialogListRow;

typedef struct {
    const char *title;
    const char *hint;
    int min_width;
    int max_width;
    int padding_x;
    int padding_y;
    bool show_lines;
    bool paged;
    int initial_selected_id;
} UiDialogListOptions;

int ui_dialog_list_find_first_selectable(const UiDialogListRow *rows, int count);
int ui_dialog_list_find_next_selectable(const UiDialogListRow *rows, int count, int start, int dir);
void ui_dialog_clamp_styled_list_view(const UiDialogListRow *rows, int count, int visible, int *top, int *selected);
int ui_dialog_styled_list_row_at(int top, int visible, int list_top_row, int window_row, const UiDialogListRow *rows, int count);
int ui_dialog_show_styled_list_modal(const UiDialogListOptions *options, const UiDialogListRow *rows, int count);
/* Shared modal helpers for short list selection and save prompts. */
int ui_dialog_show_simple_list_modal(const char *title, const char **items, int count, int initial_selected);
/* Prompt for a file name and write the result into the caller buffer. */
int ui_dialog_show_save_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz);
/* Path helpers for dialog-driven export and save flows. */
int ui_dialog_path_has_extension(const char *name, const char *ext);
/* Join a directory, filename, and extension into an output path buffer. */
int ui_dialog_build_output_path(char *outpath, size_t outpath_sz, const char *dir, const char *filename, const char *ext);

#endif
