#include "ui/internal.h"

int ui_handle_pending_grid_edit(Table *table)
{
    if (!ui_take_pending_grid_edit()) return 0;
    if (!editing_mode) return 1;

    if (cursor_row == -1) {
        edit_header_cell(table, cursor_col);
    } else {
        int actual_row = ui_actual_row_for_visible(table, cursor_row);

        if (actual_row >= 0) edit_body_cell(table, actual_row, cursor_col);
    }

    return 1;
}
