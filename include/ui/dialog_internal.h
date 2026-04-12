/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Shared dialog helpers used by modal UI flows. */

#ifndef UI_DIALOG_INTERNAL_H
#define UI_DIALOG_INTERNAL_H

#include <stddef.h>

/* Shared modal helpers for short list selection and save prompts. */
int ui_dialog_show_simple_list_modal(const char *title, const char **items, int count, int initial_selected);
int ui_dialog_show_save_filename_modal(const char *title, const char *prompt, char *out, size_t out_sz);
/* Path helpers for dialog-driven export and save flows. */
int ui_dialog_path_has_extension(const char *name, const char *ext);
int ui_dialog_build_output_path(char *outpath, size_t outpath_sz, const char *dir, const char *filename, const char *ext);

#endif
