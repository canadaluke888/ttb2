/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Main ncurses event loop for the interactive editor. */

#include <ncurses.h>
#include "ui/internal.h"
#include "core/errors.h"
#include "ui/panel_manager.h"
#include "core/workspace.h"
#include "ui/ui_history.h"

void start_ui_loop(Table *table)
{
    int ch;

    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS, NULL);
    mouseinterval(150);
    nodelay(stdscr, TRUE);
    tableview_init(&ui_table_view);

    while (1) {
        int fetched = 0;
        int final_vdir = 0;
        int vcount = 0;
        int quit = 0;

        draw_ui(table);
        wnoutrefresh(stdscr);
        pm_update();
        {
            char autosave_err[256] = {0};

            if (workspace_process_autosave(autosave_err, sizeof(autosave_err)) != 0) {
                show_error_message(autosave_err[0] ? autosave_err : "Autosave failed.");
            }
        }
        if (ui_handle_pending_grid_edit(table)) continue;

        while ((ch = getch()) != ERR) {
            if (ch == KEY_RESIZE) {
                pm_on_resize();
                continue;
            }
            if (ch == KEY_MOUSE) {
                MEVENT event;

                if (getmouse(&event) != OK) continue;
                if (event.bstate & BUTTON4_PRESSED) {
                    ch = (event.bstate & BUTTON_SHIFT) ? KEY_LEFT : KEY_UP;
                } else if (event.bstate & BUTTON5_PRESSED) {
                    ch = (event.bstate & BUTTON_SHIFT) ? KEY_RIGHT : KEY_DOWN;
                } else if (event.bstate & BUTTON1_DOUBLE_CLICKED) {
                    if (ui_handle_cell_click(table, event.x, event.y, 1)) continue;
                    continue;
                } else if (event.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED | BUTTON1_PRESSED)) {
                    if (ui_handle_cell_click(table, event.x, event.y, 0)) continue;
                    continue;
                } else {
                    continue;
                }
            }

            if (ui_search_handle_key(table, ch)) continue;

            if (!editing_mode) {
                if (ch == 'q' || ch == 'Q') {
                    quit = 1;
                    break;
                } else if (ch == 8 || ch == KEY_HOME) {
                    col_page = 0;
                    row_page = 0;
                    cursor_col = 0;
                    cursor_row = -1;
                    if (low_ram_mode && seek_mode_active()) {
                        char err[128] = {0};
                        int page = (rows_visible > 0 ? rows_visible : 200);

                        seek_mode_fetch_first(table, page, err, sizeof(err));
                    }
                } else if (ch == 'S') {
                    char serr[256] = {0};

                    if (workspace_manual_save(table, serr, sizeof(serr)) != 0) {
                        show_error_message(serr[0] ? serr : "Save failed.");
                    } else {
                        show_error_message("Workspace saved.");
                    }
                } else if (ch == 'c' || ch == 'C') {
                    prompt_add_column(table);
                } else if (ch == 'r' || ch == 'R') {
                    if (table->column_count == 0) show_error_message("You must add at least one column before adding rows.");
                    else prompt_add_row(table);
                } else if (ch == 'f' || ch == 'F') {
                    ui_search_enter(table);
                } else if (ch == 'e' || ch == 'E') {
                    ui_enter_edit_mode(table);
                } else if (ch == 'm' || ch == 'M') {
                    show_table_menu(table);
                } else if (ch == KEY_LEFT || ch == 'a' || ch == 'A') {
                    if (col_page > 0) col_page--;
                } else if (ch == KEY_RIGHT || ch == 'd' || ch == 'D') {
                    if (col_page < total_pages - 1) col_page++;
                } else if (ch == KEY_UP || ch == 'w' || ch == 'W') {
                    final_vdir = -1;
                    vcount++;
                    if (low_ram_mode && seek_mode_active()) {
                        if (cursor_row <= 0) {
                            char err[128] = {0};
                            int page = (rows_visible > 0 ? rows_visible : 200);

                            if (!fetched && seek_mode_fetch_prev(table, page, err, sizeof(err)) > 0) {
                                fetched = 1;
                                cursor_row = 0;
                            }
                        } else if (row_page > 0) {
                            row_page--;
                        }
                    } else if (row_page > 0) {
                        row_page--;
                    }
                } else if (ch == KEY_DOWN || ch == 's') {
                    final_vdir = +1;
                    vcount++;
                    if (low_ram_mode && seek_mode_active()) {
                        if (cursor_row >= table->row_count - 1) {
                            char err[128] = {0};
                            int page = (rows_visible > 0 ? rows_visible : 200);

                            if (!fetched && seek_mode_fetch_next(table, page, err, sizeof(err)) > 0) {
                                fetched = 1;
                                cursor_row = table->row_count - 1;
                            }
                        } else if (row_page < total_row_pages - 1) {
                            row_page++;
                        }
                    } else if (row_page < total_row_pages - 1) {
                        row_page++;
                    }
                }
            } else {
                if (ch == 'S') {
                    char serr[256] = {0};

                    if (workspace_manual_save(table, serr, sizeof(serr)) != 0) {
                        show_error_message(serr[0] ? serr : "Save failed.");
                    } else {
                        show_error_message("Workspace saved.");
                    }
                    continue;
                }
                if (ch == '\t') {
                    ui_advance_footer_page();
                    continue;
                }
                if (ui_reorder_active()) {
                    switch (ch) {
                        case KEY_LEFT:
                        case 'a':
                        case 'A':
                            ui_move_cursor_left_paged(table);
                            break;
                        case KEY_RIGHT:
                        case 'd':
                        case 'D':
                            ui_move_cursor_right_paged(table);
                            break;
                        case KEY_UP:
                        case 'w':
                        case 'W':
                            if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                                if (cursor_row > -1) cursor_row--;
                            } else {
                                ui_move_cursor_up_paged(table);
                            }
                            break;
                        case KEY_DOWN:
                        case 's':
                            if (reorder_mode == UI_REORDER_MOVE_COL || reorder_mode == UI_REORDER_SWAP_COL) {
                                int visible_rows = ui_visible_row_count(table);
                                if (cursor_row < visible_rows - 1) cursor_row++;
                            } else {
                                ui_move_cursor_down_paged(table);
                            }
                            break;
                        case '\n':
                            if (reorder_mode == UI_REORDER_MOVE_ROW || reorder_mode == UI_REORDER_SWAP_ROW) {
                                int source_visible = reorder_source_row;
                                int source_actual = ui_actual_row_for_visible(table, source_visible);
                                int dest_visible = cursor_row;
                                int dest_actual = ui_actual_row_for_visible(table, dest_visible);
                                char err[256] = {0};

                                if (source_visible < 0 || dest_visible < 0 || source_actual < 0 || dest_actual < 0) {
                                    show_error_message("Invalid row selection.");
                                    ui_clear_reorder_mode();
                                    break;
                                }
                                if (source_visible == dest_visible) {
                                    show_error_message("Source and destination row are the same.");
                                    ui_clear_reorder_mode();
                                    break;
                                }
                                if (reorder_mode == UI_REORDER_MOVE_ROW) {
                                    int placement = prompt_move_row_placement(table, source_actual, dest_actual);
                                    if (placement == 0 || placement == 1) {
                                        UiHistoryApplyResult result = {0};
                                        if (ui_history_move_row(table, source_actual, dest_actual, placement == 1, &result, err, sizeof(err)) != 0) {
                                            show_error_message(err[0] ? err : "Failed to move row.");
                                        } else {
                                            if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) show_error_message(err);
                                            ui_clear_reorder_mode();
                                        }
                                    }
                                } else {
                                    UiHistoryApplyResult result = {0};
                                    if (ui_history_swap_rows(table, source_actual, dest_actual, &result, err, sizeof(err)) != 0) {
                                        show_error_message(err[0] ? err : "Failed to swap rows.");
                                    } else {
                                        if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) show_error_message(err);
                                        ui_clear_reorder_mode();
                                    }
                                }
                            } else {
                                int source_col = reorder_source_col;
                                int dest_col = cursor_col;
                                char err[256] = {0};

                                if (source_col < 0 || dest_col < 0 || source_col >= table->column_count || dest_col >= table->column_count) {
                                    show_error_message("Invalid column selection.");
                                    ui_clear_reorder_mode();
                                    break;
                                }
                                if (source_col == dest_col) {
                                    show_error_message("Source and destination column are the same.");
                                    ui_clear_reorder_mode();
                                    break;
                                }
                                if (reorder_mode == UI_REORDER_MOVE_COL) {
                                    int placement = prompt_move_column_placement(table, source_col, dest_col);
                                    if (placement == 0 || placement == 1) {
                                        UiHistoryApplyResult result = {0};
                                        if (ui_history_move_column(table, source_col, dest_col, placement == 1, &result, err, sizeof(err)) != 0) {
                                            show_error_message(err[0] ? err : "Failed to move column.");
                                        } else {
                                            if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) show_error_message(err);
                                            ui_clear_reorder_mode();
                                        }
                                    }
                                } else {
                                    UiHistoryApplyResult result = {0};
                                    if (ui_history_swap_columns(table, source_col, dest_col, &result, err, sizeof(err)) != 0) {
                                        show_error_message(err[0] ? err : "Failed to swap columns.");
                                    } else {
                                        if (ui_history_refresh(table, &result, err, sizeof(err)) != 0 && err[0]) show_error_message(err);
                                        ui_clear_reorder_mode();
                                    }
                                }
                            }
                            break;
                        case 27:
                            ui_clear_reorder_mode();
                            break;
                        default:
                            break;
                    }
                    continue;
                }
                if (del_row_mode) {
                    if (ui_visible_row_count(table) <= 0) del_row_mode = 0;
                    switch (ch) {
                        case KEY_UP:
                        case 'w':
                        case 'W': {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            if (rmin < 0) rmin = 0;
                            if (cursor_row > rmin) cursor_row--;
                            break;
                        }
                        case KEY_DOWN:
                        case 's': {
                            int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                            int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
                            if (rmax >= ui_visible_row_count(table)) rmax = ui_visible_row_count(table) - 1;
                            if (cursor_row < rmax) cursor_row++;
                            break;
                        }
                        case '\n':
                            confirm_delete_row_at(table, ui_actual_row_for_visible(table, cursor_row));
                            if (cursor_row >= ui_visible_row_count(table)) cursor_row = ui_visible_row_count(table) - 1;
                            if (cursor_row < 0) {
                                cursor_row = -1;
                                editing_mode = (ui_visible_row_count(table) > 0);
                            }
                            del_row_mode = 0;
                            break;
                        case 27:
                            del_row_mode = 0;
                            break;
                        case KEY_LEFT:
                            if (cursor_col > col_start) cursor_col--;
                            break;
                        case KEY_RIGHT: {
                            int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
                            if (cursor_col < cmax) cursor_col++;
                            break;
                        }
                        default:
                            break;
                    }
                    continue;
                }
                if (del_col_mode) {
                    if (table->column_count <= 0) del_col_mode = 0;
                    switch (ch) {
                        case KEY_LEFT:
                        case 'a':
                        case 'A':
                            if (cursor_col > col_start) cursor_col--;
                            break;
                        case KEY_RIGHT:
                        case 'd':
                        case 'D': {
                            int cmax = (cols_visible > 0) ? (col_start + cols_visible - 1) : (table->column_count - 1);
                            if (cursor_col < cmax) cursor_col++;
                            break;
                        }
                        case '\n':
                            confirm_delete_column_at(table, cursor_col);
                            if (cursor_col >= table->column_count) cursor_col = table->column_count - 1;
                            if (cursor_col < 0) cursor_col = 0;
                            del_col_mode = 0;
                            break;
                        case 27:
                            del_col_mode = 0;
                            break;
                        case KEY_UP:
                        case KEY_DOWN:
                            if (ch == KEY_UP) {
                                if (cursor_row > -1) {
                                    int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                                    if (rmin < 0) rmin = 0;
                                    if (cursor_row > rmin) cursor_row--;
                                }
                            } else {
                                int rmin = row_page * (rows_visible > 0 ? rows_visible : 1);
                                int rmax = rmin + (rows_visible > 0 ? rows_visible - 1 : 0);
                                if (rmax >= ui_visible_row_count(table)) rmax = ui_visible_row_count(table) - 1;
                                if (cursor_row < rmax) cursor_row++;
                            }
                            break;
                        default:
                            break;
                    }
                    continue;
                }

                switch (ch) {
                    case KEY_LEFT:
                    case 'a':
                    case 'A':
                        ui_move_cursor_left_cross_page(table);
                        break;
                    case KEY_RIGHT:
                    case 'd':
                    case 'D':
                        ui_move_cursor_right_cross_page(table);
                        break;
                    case KEY_UP:
                    case 'w':
                    case 'W':
                        final_vdir = -1;
                        vcount++;
                        if (low_ram_mode && seek_mode_active()) {
                            if (cursor_row <= 0) {
                                char err[128] = {0};
                                int page = (rows_visible > 0 ? rows_visible : 200);
                                if (!fetched && seek_mode_fetch_prev(table, page, err, sizeof(err)) > 0) {
                                    fetched = 1;
                                    cursor_row = 0;
                                } else if (cursor_row > -1) {
                                    cursor_row--;
                                }
                            } else {
                                cursor_row--;
                            }
                        } else {
                            ui_move_cursor_up_cross_page(table);
                        }
                        break;
                    case KEY_DOWN:
                    case 's':
                        final_vdir = +1;
                        vcount++;
                        if (low_ram_mode && seek_mode_active()) {
                            if (cursor_row >= table->row_count - 1) {
                                char err[128] = {0};
                                int page = (rows_visible > 0 ? rows_visible : 200);
                                if (!fetched && seek_mode_fetch_next(table, page, err, sizeof(err)) > 0) {
                                    fetched = 1;
                                    cursor_row = table->row_count - 1;
                                }
                            } else {
                                cursor_row++;
                            }
                        } else {
                            ui_move_cursor_down_cross_page(table);
                        }
                        break;
                    case 27:
                        ui_clear_reorder_mode();
                        footer_page = 0;
                        editing_mode = 0;
                        break;
                    case 8:
                    case KEY_HOME:
                        col_page = 0;
                        row_page = 0;
                        cursor_col = col_start;
                        cursor_row = row_page * (rows_visible > 0 ? rows_visible : 1);
                        if (cursor_row >= ui_visible_row_count(table)) cursor_row = ui_visible_row_count(table) - 1;
                        if (cursor_row < 0) cursor_row = -1;
                        if (low_ram_mode && seek_mode_active()) {
                            char err[128] = {0};
                            int page = (rows_visible > 0 ? rows_visible : 200);
                            seek_mode_fetch_first(table, page, err, sizeof(err));
                        }
                        break;
                    case 21: {
                        UiHistoryApplyResult result = {0};
                        char err[256] = {0};
                        if (ui_history_undo(table, &result, err, sizeof(err)) != 0) {
                            show_error_message(err[0] ? err : "Nothing to undo.");
                        } else if (ui_history_refresh(table, &result, err, sizeof(err)) != 0) {
                            show_error_message(err[0] ? err : "Failed to refresh after undo.");
                        }
                        break;
                    }
                    case 18: {
                        UiHistoryApplyResult result = {0};
                        char err[256] = {0};
                        if (ui_history_redo(table, &result, err, sizeof(err)) != 0) {
                            show_error_message(err[0] ? err : "Nothing to redo.");
                        } else if (ui_history_refresh(table, &result, err, sizeof(err)) != 0) {
                            show_error_message(err[0] ? err : "Failed to refresh after redo.");
                        }
                        break;
                    }
                    case 'm':
                    case 'M':
                        show_table_menu(table);
                        break;
                    case 'f':
                    case 'F':
                        ui_search_enter(table);
                        break;
                    case '\n':
                        if (cursor_row == -1) edit_header_cell(table, cursor_col);
                        else edit_body_cell(table, ui_actual_row_for_visible(table, cursor_row), cursor_col);
                        break;
                    case 'x':
                        if (ui_visible_row_count(table) <= 0) {
                            show_error_message("No rows to delete.");
                            break;
                        }
                        if (cursor_row < 0) cursor_row = 0;
                        ui_clear_reorder_mode();
                        del_row_mode = 1;
                        del_col_mode = 0;
                        break;
                    case 'v':
                        if (cursor_row < 0) {
                            if (table->column_count <= 0) {
                                show_error_message("No columns to move.");
                                break;
                            }
                            ui_clear_reorder_mode();
                            reorder_mode = UI_REORDER_MOVE_COL;
                            reorder_source_col = cursor_col;
                        } else {
                            if (ui_visible_row_count(table) <= 0) {
                                show_error_message("No rows to move.");
                                break;
                            }
                            ui_clear_reorder_mode();
                            reorder_mode = UI_REORDER_MOVE_ROW;
                            reorder_source_row = cursor_row;
                        }
                        del_row_mode = 0;
                        del_col_mode = 0;
                        break;
                    case '[': {
                        int insert_row = 0;
                        if (table->column_count <= 0) {
                            show_error_message("Add at least one column first.");
                            break;
                        }
                        if (cursor_row >= 0) {
                            insert_row = ui_actual_row_for_visible(table, cursor_row);
                            if (insert_row < 0) insert_row = 0;
                        } else if (ui_visible_row_count(table) > 0) {
                            insert_row = ui_actual_row_for_visible(table, 0);
                            if (insert_row < 0) insert_row = 0;
                        }
                        prompt_insert_row_at(table, insert_row);
                        break;
                    }
                    case ']': {
                        int insert_row = 0;
                        if (table->column_count <= 0) {
                            show_error_message("Add at least one column first.");
                            break;
                        }
                        if (cursor_row >= 0) {
                            insert_row = ui_actual_row_for_visible(table, cursor_row);
                            if (insert_row < 0) insert_row = table->row_count;
                            else insert_row++;
                        } else if (ui_visible_row_count(table) > 0) {
                            insert_row = ui_actual_row_for_visible(table, 0);
                            if (insert_row < 0) insert_row = 0;
                        }
                        prompt_insert_row_at(table, insert_row);
                        break;
                    }
                    case 'X':
                        if (table->column_count <= 0) {
                            show_error_message("No columns to delete.");
                            break;
                        }
                        if (table->column_count == 1) {
                            show_error_message("Cannot delete the last column.");
                            break;
                        }
                        if (cursor_col < 0) cursor_col = 0;
                        ui_clear_reorder_mode();
                        del_col_mode = 1;
                        del_row_mode = 0;
                        break;
                    case 'V':
                        if (cursor_row < 0) {
                            if (table->column_count <= 0) {
                                show_error_message("No columns to swap.");
                                break;
                            }
                            ui_clear_reorder_mode();
                            reorder_mode = UI_REORDER_SWAP_COL;
                            reorder_source_col = cursor_col;
                        } else {
                            if (ui_visible_row_count(table) <= 0) {
                                show_error_message("No rows to swap.");
                                break;
                            }
                            ui_clear_reorder_mode();
                            reorder_mode = UI_REORDER_SWAP_ROW;
                            reorder_source_row = cursor_row;
                        }
                        del_row_mode = 0;
                        del_col_mode = 0;
                        break;
                    case '{': {
                        int insert_col = (table->column_count > 0) ? cursor_col : 0;
                        if (insert_col < 0) insert_col = 0;
                        prompt_insert_column_at(table, insert_col);
                        break;
                    }
                    case '}': {
                        int insert_col = (table->column_count > 0) ? (cursor_col + 1) : 0;
                        if (insert_col < 0) insert_col = 0;
                        if (insert_col > table->column_count) insert_col = table->column_count;
                        prompt_insert_column_at(table, insert_col);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        if (editing_mode && final_vdir != 0 && rows_visible > 0 && vcount >= 3) {
            int rstart = row_page * rows_visible;
            int rend = rstart + rows_visible - 1;

            if (rend >= ui_visible_row_count(table)) rend = ui_visible_row_count(table) - 1;
            if (final_vdir > 0) {
                if (rend >= 0) cursor_row = rend;
            } else if (final_vdir < 0) {
                if (rstart < ui_visible_row_count(table)) cursor_row = (rstart >= 0 ? rstart : 0);
            }
        }
        if (quit) break;

        ui_clamp_cursor_viewport(table);
        napms(16);
    }
}
