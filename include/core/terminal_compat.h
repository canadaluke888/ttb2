/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

#ifndef TTB2_TERMINAL_COMPAT_H
#define TTB2_TERMINAL_COMPAT_H

/* Apply platform terminal defaults before ncurses/PDCurses initialization. */
void terminal_compat_prepare(void);

#endif
