/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Cell formatting helpers for the grid and status displays. */

#include <stdio.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

int ui_numeric_column_uses_sign_slot(const Table *table, int col)
{
    if (!table || col < 0 || col >= table->column_count) return 0;
    return table->columns[col].type == TYPE_INT || table->columns[col].type == TYPE_FLOAT;
}

int ui_numeric_text_width_for_grid(const char *text)
{
    if (!text || !*text) return 0;
    return 2 + ui_text_width((*text == '-') ? text + 1 : text);
}

int ui_format_cell_value(const Table *table, int row, int col, char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return -1;
    buf[0] = '\0';
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count) return -1;
    if (!table->rows[row].values || !table->rows[row].values[col]) return 0;

    if (table->columns[col].type == TYPE_INT) {
        snprintf(buf, buf_sz, "%d", *(int *)table->rows[row].values[col]);
    } else if (table->columns[col].type == TYPE_FLOAT) {
        snprintf(buf, buf_sz, "%.2f", *(float *)table->rows[row].values[col]);
    } else if (table->columns[col].type == TYPE_BOOL) {
        snprintf(buf, buf_sz, "%s", (*(int *)table->rows[row].values[col]) ? "true" : "false");
    } else {
        snprintf(buf, buf_sz, "%s", (char *)table->rows[row].values[col]);
    }

    return 0;
}
