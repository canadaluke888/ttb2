/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Top-level UI entry points and runtime feature toggles. */

#ifndef UI_H
#define UI_H

#include "data/table.h"
/* Initialize global UI colors and open paths into the current table context. */
void init_colors(void);
/* Open a file or workspace path into the current editing session. */
int ui_open_path(Table *table, const char *path, int preserve_current_table, int show_book_success);
/* Run the main ncurses event loop until the user exits. */
void start_ui_loop(Table *table);
/* Enable or disable the row-number gutter in the grid renderer. */
void ui_set_row_gutter_enabled(int enabled);
int ui_row_gutter_enabled(void);

#endif
