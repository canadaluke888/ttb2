#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "data/table.h"
#include "data/table_ops.h"
#include "ui/internal.h"
#include "ui/ui_history.h"

int cursor_row = -1;
int cursor_col = 0;
int rows_visible = 10;
int cols_visible = 10;
int row_page = 0;
int col_page = 0;
int total_pages = 1;
int total_row_pages = 1;
int editing_mode = 0;
int search_mode = 0;
int search_hit_count = 0;
int search_hit_index = 0;
int search_sel_start = -1;
int search_sel_len = 0;
int low_ram_mode = 0;
int row_gutter_enabled = 1;
int footer_page = 0;
int del_row_mode = 0;
int del_col_mode = 0;
UiReorderMode reorder_mode = UI_REORDER_NONE;
int reorder_source_row = -1;
int reorder_source_col = -1;
TableView ui_table_view;
char search_query[128];

int db_autosave_table(const Table *t, char *err, size_t err_sz)
{
    (void)t;
    if (err && err_sz > 0) err[0] = '\0';
    return 0;
}

int ui_visible_row_count(Table *table)
{
    return table ? table->row_count : 0;
}

int ui_actual_row_for_visible(Table *table, int visible_row)
{
    (void)table;
    return visible_row;
}

int ui_table_view_is_active(void)
{
    return 0;
}

int ui_rebuild_table_view(Table *table, char *err, size_t err_sz)
{
    (void)table;
    if (err && err_sz > 0) err[0] = '\0';
    return 0;
}

void ui_focus_location(Table *table, int actual_row, int col, int prefer_header)
{
    (void)table;
    cursor_row = prefer_header ? -1 : actual_row;
    cursor_col = col;
}

static void expect_int_cell(Table *table, int row, int col, int expected)
{
    assert(table->rows[row].values[col] != NULL);
    assert(*(int *)table->rows[row].values[col] == expected);
}

static void expect_str_cell(Table *table, int row, int col, const char *expected)
{
    assert(table->rows[row].values[col] != NULL);
    assert(strcmp((char *)table->rows[row].values[col], expected) == 0);
}

static void test_cell_undo_redo(void)
{
    UiHistoryApplyResult result = {0};
    char err[256] = {0};
    const char *row0[] = {"1", "alpha"};
    Table *table = create_table("History");

    assert(tableop_insert_column(table, "id", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "name", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);

    ui_history_reset();
    assert(ui_history_set_cell(table, 0, 1, "beta", &result, err, sizeof(err)) == 0);
    expect_str_cell(table, 0, 1, "beta");
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    expect_str_cell(table, 0, 1, "alpha");
    assert(ui_history_redo(table, &result, err, sizeof(err)) == 0);
    expect_str_cell(table, 0, 1, "beta");

    free_table(table);
}

static void test_row_insert_delete_undo_redo(void)
{
    UiHistoryApplyResult result = {0};
    char err[256] = {0};
    const char *row0[] = {"1", "one"};
    const char *row1[] = {"2", "two"};
    Table *table = create_table("Rows");

    assert(tableop_insert_column(table, "id", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "label", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);

    ui_history_reset();
    assert(ui_history_insert_row(table, 1, row1, &result, err, sizeof(err)) == 0);
    assert(table->row_count == 2);
    expect_int_cell(table, 1, 0, 2);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(table->row_count == 1);
    expect_int_cell(table, 0, 0, 1);
    assert(ui_history_redo(table, &result, err, sizeof(err)) == 0);
    assert(table->row_count == 2);
    expect_str_cell(table, 1, 1, "two");

    assert(ui_history_delete_row(table, 0, &result, err, sizeof(err)) == 0);
    assert(table->row_count == 1);
    expect_int_cell(table, 0, 0, 2);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(table->row_count == 2);
    expect_int_cell(table, 0, 0, 1);

    free_table(table);
}

static void test_column_delete_restore(void)
{
    UiHistoryApplyResult result = {0};
    char err[256] = {0};
    const char *row0[] = {"1", "alpha"};
    Table *table = create_table("Cols");

    assert(tableop_insert_column(table, "id", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "name", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);

    ui_history_reset();
    assert(ui_history_delete_column(table, 1, &result, err, sizeof(err)) == 0);
    assert(table->column_count == 1);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(table->column_count == 2);
    assert(strcmp(table->columns[1].name, "name") == 0);
    expect_str_cell(table, 0, 1, "alpha");
    assert(ui_history_redo(table, &result, err, sizeof(err)) == 0);
    assert(table->column_count == 1);

    free_table(table);
}

static void test_moves_swaps_and_type_change(void)
{
    UiHistoryApplyResult result = {0};
    char err[256] = {0};
    const char *row0[] = {"1", "3.5", "alpha"};
    const char *row1[] = {"2", "4.5", "beta"};
    Table *table = create_table("Moves");

    assert(tableop_insert_column(table, "id", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "score", TYPE_FLOAT, err, sizeof(err)) == 0);
    assert(tableop_insert_column(table, "name", TYPE_STR, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row1, err, sizeof(err)) == 0);

    ui_history_reset();
    assert(ui_history_move_row(table, 0, 1, 1, &result, err, sizeof(err)) == 0);
    expect_int_cell(table, 1, 0, 1);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    expect_int_cell(table, 0, 0, 1);

    assert(ui_history_swap_columns(table, 0, 2, &result, err, sizeof(err)) == 0);
    assert(strcmp(table->columns[0].name, "name") == 0);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(strcmp(table->columns[0].name, "id") == 0);

    assert(ui_history_change_column_type(table, 1, TYPE_STR, &result, err, sizeof(err)) == 0);
    assert(table->columns[1].type == TYPE_STR);
    expect_str_cell(table, 0, 1, "3.5");
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(table->columns[1].type == TYPE_FLOAT);

    free_table(table);
}

static void test_redo_cleared_after_new_edit(void)
{
    UiHistoryApplyResult result = {0};
    char err[256] = {0};
    const char *row0[] = {"1"};
    Table *table = create_table("Redo");

    assert(tableop_insert_column(table, "id", TYPE_INT, err, sizeof(err)) == 0);
    assert(tableop_insert_row(table, row0, err, sizeof(err)) == 0);

    ui_history_reset();
    assert(ui_history_set_cell(table, 0, 0, "2", &result, err, sizeof(err)) == 0);
    assert(ui_history_undo(table, &result, err, sizeof(err)) == 0);
    assert(ui_history_can_redo());
    assert(ui_history_set_cell(table, 0, 0, "3", &result, err, sizeof(err)) == 0);
    assert(!ui_history_can_redo());
    expect_int_cell(table, 0, 0, 3);

    free_table(table);
}

int main(void)
{
    test_cell_undo_redo();
    test_row_insert_delete_undo_redo();
    test_column_delete_restore();
    test_moves_swaps_and_type_change();
    test_redo_cleared_after_new_edit();
    puts("history tests passed");
    return 0;
}
