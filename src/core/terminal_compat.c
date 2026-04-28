/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Platform terminal setup for curses frontends. */

#include "core/terminal_compat.h"

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>

static void enable_console_mode_flag(HANDLE handle, DWORD flag)
{
    DWORD mode = 0;

    if (handle == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(handle, &mode)) return;
    SetConsoleMode(handle, mode | flag);
}

static void disable_console_mode_flag(HANDLE handle, DWORD flag)
{
    DWORD mode = 0;

    if (handle == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(handle, &mode)) return;
#ifdef ENABLE_EXTENDED_FLAGS
    mode |= ENABLE_EXTENDED_FLAGS;
#endif
    SetConsoleMode(handle, mode & ~flag);
}

void terminal_compat_prepare(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    enable_console_mode_flag(out, ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
#ifdef DISABLE_NEWLINE_AUTO_RETURN
    enable_console_mode_flag(out, DISABLE_NEWLINE_AUTO_RETURN);
#endif
#ifdef ENABLE_VIRTUAL_TERMINAL_INPUT
    enable_console_mode_flag(in, ENABLE_VIRTUAL_TERMINAL_INPUT);
#endif
#ifdef ENABLE_QUICK_EDIT_MODE
    disable_console_mode_flag(in, ENABLE_QUICK_EDIT_MODE);
#endif

    if (!getenv("TERM")) {
        _putenv_s("TERM", "xterm-256color");
    }
    _putenv_s("LANG", "C.UTF-8");
}
#else
void terminal_compat_prepare(void)
{
}
#endif
