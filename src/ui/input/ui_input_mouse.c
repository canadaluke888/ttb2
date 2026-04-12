#include <stdlib.h>
#include "ui/internal.h"

int ui_handle_cell_click(Table *table, int mouse_x, int mouse_y, int activate_editor)
{
    int visible_rows;
    int header_y = 3;
    int body_top_y = 5;
    int x = 2;
    int clicked_row = -2;
    int *col_widths;
    UiGridLayout layout;
    int cell_left;
    int clicked_col = -1;

    if (!table || table->column_count <= 0) return 0;
    if (search_mode || ui_reorder_active() || del_row_mode || del_col_mode) return 0;

    visible_rows = ui_visible_row_count(table);
    if (mouse_y == header_y) {
        clicked_row = -1;
    } else if (mouse_y >= body_top_y && ((mouse_y - body_top_y) % 2) == 0) {
        clicked_row = row_page * (rows_visible > 0 ? rows_visible : 1) + ((mouse_y - body_top_y) / 2);
        if (clicked_row < 0 || clicked_row >= visible_rows) return 0;
    } else {
        return 0;
    }

    if (ui_alloc_column_widths(table, &col_widths) != 0) return 0;
    ui_fill_grid_layout(table, &layout);

    cell_left = x + 1;
    if (layout.use_gutter) cell_left += layout.gutter_width + 1;

    for (int j = col_start; j < table->column_count && j < col_start + cols_visible; ++j) {
        int cell_right = cell_left + col_widths[j] - 1;

        if (mouse_x >= cell_left && mouse_x <= cell_right) {
            clicked_col = j;
            break;
        }
        cell_left = cell_right + 2;
    }

    free(col_widths);

    if (clicked_col < 0) return 0;

    cursor_row = clicked_row;
    cursor_col = clicked_col;
    if (!editing_mode) ui_clear_reorder_mode();
    editing_mode = 1;
    footer_page = 0;
    if (cursor_row >= 0) ui_ensure_cursor_row_visible(table);
    ui_ensure_cursor_column_visible(table);

    if (activate_editor) ui_request_pending_grid_edit();

    return 1;
}
