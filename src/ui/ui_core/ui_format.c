/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Cell formatting helpers for the grid and status displays. */

#include <stdio.h>
#include <string.h>
#include "ui/internal.h"
#include "ui/ui_text.h"

static void format_grouped_number(const char *text, char *buf, size_t buf_sz)
{
    const char *digits;
    const char *dot;
    size_t sign_len = 0;
    size_t int_len;
    size_t commas;
    size_t frac_len;
    size_t needed;
    size_t out = 0;
    size_t first_group;

    if (!buf || buf_sz == 0) return;
    buf[0] = '\0';
    if (!text || !*text) return;

    digits = text;
    if (*digits == '-') {
        sign_len = 1;
        digits++;
    }

    dot = strchr(digits, '.');
    int_len = dot ? (size_t)(dot - digits) : strlen(digits);
    frac_len = dot ? strlen(dot) : 0;
    commas = (int_len > 0) ? ((int_len - 1) / 3) : 0;
    needed = sign_len + int_len + commas + frac_len + 1;
    if (needed > buf_sz) {
        snprintf(buf, buf_sz, "%s", text);
        return;
    }

    if (sign_len > 0) buf[out++] = '-';
    if (int_len == 0) {
        buf[out++] = '0';
    } else {
        first_group = int_len % 3;
        if (first_group == 0) first_group = 3;

        for (size_t i = 0; i < int_len; ++i) {
            if (i > 0 && i == first_group) buf[out++] = ',';
            else if (i > first_group && ((i - first_group) % 3) == 0) buf[out++] = ',';
            buf[out++] = digits[i];
        }
    }

    if (frac_len > 0) {
        memcpy(buf + out, dot, frac_len);
        out += frac_len;
    }
    buf[out] = '\0';
}

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
        char raw[64];

        snprintf(raw, sizeof(raw), "%d", *(int *)table->rows[row].values[col]);
        format_grouped_number(raw, buf, buf_sz);
    } else if (table->columns[col].type == TYPE_FLOAT) {
        char raw[64];

        snprintf(raw, sizeof(raw), "%.2f", *(float *)table->rows[row].values[col]);
        format_grouped_number(raw, buf, buf_sz);
    } else if (table->columns[col].type == TYPE_BOOL) {
        snprintf(buf, buf_sz, "%s", (*(int *)table->rows[row].values[col]) ? "true" : "false");
    } else {
        snprintf(buf, buf_sz, "%s", (char *)table->rows[row].values[col]);
    }

    return 0;
}
