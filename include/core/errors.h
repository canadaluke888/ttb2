/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Error dialog helpers for the ncurses UI. */

#ifndef ERRORS_H
#define ERRORS_H

/* Display a blocking modal error message until the user acknowledges it. */
void show_error_message(const char *msg);

#endif
