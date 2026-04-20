/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Regression checks for styled dialog list selection and paging helpers. */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "ui/dialog_internal.h"
#include "ui/panel_manager.h"

PmNode *pm_add(int y, int x, int h, int w, int z_index, PmLayerRole role)
{
    (void)y; (void)x; (void)h; (void)w; (void)z_index; (void)role;
    return NULL;
}

void pm_remove(PmNode *n)
{
    (void)n;
}

void pm_update(void)
{
}

void pm_wnoutrefresh(PmNode *n)
{
    (void)n;
}

int show_text_input_modal(const char *title,
                          const char *hint,
                          const char *prompt,
                          char *out,
                          size_t out_sz,
                          _Bool allow_empty)
{
    (void)title;
    (void)hint;
    (void)prompt;
    (void)out;
    (void)out_sz;
    (void)allow_empty;
    return -1;
}

static void test_selectable_navigation(void)
{
    const UiDialogListRow rows[] = {
        {UI_DIALOG_LIST_ROW_HEADER, "General", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_SPACER, "", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Autosave", 11, 1, 0},
        {UI_DIALOG_LIST_ROW_DIVIDER, "", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Theme", 12, 1, 0}
    };

    assert(ui_dialog_list_find_first_selectable(rows, 5) == 2);
    assert(ui_dialog_list_find_next_selectable(rows, 5, 2, 1) == 4);
    assert(ui_dialog_list_find_next_selectable(rows, 5, 4, 1) == 2);
    assert(ui_dialog_list_find_next_selectable(rows, 5, 2, -1) == 4);
}

static void test_view_clamping(void)
{
    const UiDialogListRow rows[] = {
        {UI_DIALOG_LIST_ROW_HEADER, "Header", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "One", 1, 1, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Two", 2, 1, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Three", 3, 1, 0},
        {UI_DIALOG_LIST_ROW_SPACER, "", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Four", 4, 1, 0}
    };
    int top = 0;
    int selected = 5;

    ui_dialog_clamp_styled_list_view(rows, 6, 3, &top, &selected);
    assert(selected == 5);
    assert(top == 3);

    selected = 0;
    top = 0;
    ui_dialog_clamp_styled_list_view(rows, 6, 3, &top, &selected);
    assert(selected == 1);
    assert(top == 0);
}

static void test_row_hit_testing(void)
{
    const UiDialogListRow rows[] = {
        {UI_DIALOG_LIST_ROW_HEADER, "Header", -1, 0, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "One", 1, 1, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Two", 2, 1, 0},
        {UI_DIALOG_LIST_ROW_ITEM, "Three", 3, 1, 0}
    };

    assert(ui_dialog_styled_list_row_at(1, 2, 4, 4, rows, 4) == 1);
    assert(ui_dialog_styled_list_row_at(1, 2, 4, 5, rows, 4) == 2);
    assert(ui_dialog_styled_list_row_at(1, 2, 4, 3, rows, 4) == -1);
    assert(ui_dialog_styled_list_row_at(1, 2, 4, 6, rows, 4) == -1);
}

int main(void)
{
    test_selectable_navigation();
    test_view_clamping();
    test_row_hit_testing();
    puts("ui dialog list tests passed");
    return 0;
}
