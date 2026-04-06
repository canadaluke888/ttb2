#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#include "panel_manager.h"
#include "errors.h"
#include "db_manager.h"
#include "workspace.h"
#include "ui.h"

// Active DB connection managed via db_manager singleton helpers

static bool table_has_schema(const Table *table)
{
    return table && table->column_count > 0;
}

static void draw_list_modal(const char *title, const char **items, int count, int *io_selected) {
    int min_h = 8;
    int h = count + 4;
    if (h < min_h) h = min_h;
    int max_h = LINES - 2;
    if (h > max_h) h = max_h;

    int w = COLS - 4;
    if (w < 24) w = COLS - 2;
    if (w < 24) w = 24;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    int selected = (io_selected && *io_selected >= 0 && *io_selected < count) ? *io_selected : 0;
    if (count <= 0) {
        selected = -1;
    }

    int visible = h - 4;
    if (visible < 1) visible = 1;
    int top = 0;
    if (selected >= visible) {
        top = selected - visible + 1;
    }

    int ch;
    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "%s", title ? title : "");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwhline(modal->win, 2, 1, ACS_HLINE, w - 2);
        mvwaddch(modal->win, 2, 0, ACS_LTEE);
        mvwaddch(modal->win, 2, w - 1, ACS_RTEE);
        int drawn = 0;
        for (; drawn < visible && (top + drawn) < count; ++drawn) {
            int idx = top + drawn;
            int row = 3 + drawn;
            if (idx == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, row, 2, "%s", items[idx]);
            if (idx == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        for (int i = drawn; i < visible; ++i) {
            int row = 3 + i;
            if (row >= h - 1) break;
            mvwhline(modal->win, row, 1, ' ', w - 2);
        }
        pm_wnoutrefresh(shadow);
        pm_wnoutrefresh(modal);
        pm_update();

        ch = wgetch(modal->win);
        if (ch == KEY_UP) {
            if (count > 0) {
                selected = (selected > 0) ? selected - 1 : count - 1;
                if (selected < top) {
                    top = selected;
                }
            }
        }
        else if (ch == KEY_DOWN) {
            if (count > 0) {
                selected = (selected + 1) % count;
                if (selected >= top + visible) {
                    top = selected - visible + 1;
                }
            }
        }
        else if (ch == '\n') break;
        else if (ch == 27) { selected = -1; break; }
    }
    if (io_selected) *io_selected = selected;
    pm_remove(modal); pm_remove(shadow); pm_update();
}

static int prompt_text_input(const char *title, const char *prompt, char *out, size_t out_sz) {
    return show_text_input_modal(title,
                                 "[Enter] Confirm   [Esc] Cancel",
                                 prompt,
                                 out,
                                 out_sz,
                                 false);
}

static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i) free(list[i]);
    free(list);
}

void show_db_manager(Table *table) {
    noecho(); curs_set(0);

    const char *menu[] = { "Connect", "Create", "Delete DB", "Load Table", "Delete Table", "Close", "Back" };
    int menu_count = (int)(sizeof(menu) / sizeof(menu[0]));
    int selected = 0;

    while (1) {
        draw_list_modal("Database Manager", menu, menu_count, &selected);
        if (selected < 0 || selected == menu_count - 1) {
            // Back / Esc
            return;
        }

        char err[256] = {0};
        if (strcmp(menu[selected], "Connect") == 0) {
            char **dbs = NULL; int n = 0;
            if (db_list_databases(&dbs, &n, err, sizeof(err)) != 0) { show_error_message(err); continue; }
            if (n == 0) { show_error_message("No databases found."); free(dbs); continue; }
            const char **items = (const char**)dbs; int pick = 0;
            draw_list_modal("Select a database to connect", items, n, &pick);
            if (pick >= 0) {
                // Build full path: databases/<name>
                char path[1024];
                char cwd[512]; getcwd(cwd, sizeof(cwd));
                snprintf(path, sizeof(path), "%s/databases/%s", cwd, dbs[pick]);
                DbManager *cur = db_get_active();
                if (cur) { db_close(cur); db_set_active(NULL); }
                DbManager *conn = db_open(path, err, sizeof(err));
                if (!conn) show_error_message(err);
                else {
                    db_set_active(conn);
                    size_t mlen = strlen(path) + 32;
                    char *msg = (char*)malloc(mlen);
                    if (msg) {
                        snprintf(msg, mlen, "Connected to: %s", path);
                        show_error_message(msg);
                        free(msg);
                    } else {
                        show_error_message("Connected to database");
                    }
                    // Sync check: if in-memory table exists and a table of the same name exists in DB, prompt how to resolve
                    if (table && table->name && table->name[0]) {
                        if (db_table_exists(conn, table->name)) {
                            const char *opts[] = {
                                "Overwrite DB with memory table",
                                "Load DB table into memory",
                                "Skip"
                            };
                            int pick = 0;
                            draw_list_modal("Name conflict: choose action", opts, 3, &pick);
                            if (pick == 0) {
                                if (table_has_schema(table)) {
                                    char serr[256] = {0};
                                    if (db_save_table(conn, table, serr, sizeof(serr)) != 0) {
                                        show_error_message(serr[0] ? serr : "Save failed");
                                    } else {
                                        show_error_message("Database updated from memory table.");
                                    }
                                } else {
                                    show_error_message("Current table has no columns to save.");
                                }
                            } else if (pick == 1) {
                                char lerr[256] = {0};
                                Table *loaded = db_load_table(conn, table->name, lerr, sizeof(lerr));
                                if (!loaded) {
                                    show_error_message(lerr[0] ? lerr : "Load failed");
                                } else {
                                    replace_table_contents(table, loaded);
                                    workspace_manual_save(table, NULL, 0);
                                    show_error_message("Memory table replaced from database.");
                                }
                            } else {
                                // Skip
                            }
                        } else if (table_has_schema(table)) {
                            // No conflict and table has structure: save current in-memory table into DB
                            char serr[256] = {0};
                            db_save_table(conn, table, serr, sizeof(serr));
                        }
                    }
                }
            }
            free_string_list(dbs, n);
        } else if (strcmp(menu[selected], "Create") == 0) {
            char name[128] = {0};
            if (prompt_text_input("Create database", "Name (without .db): ", name, sizeof(name)) <= 0) continue;
            if (!strstr(name, ".db")) strcat(name, ".db");
            if (db_create_database(name, err, sizeof(err)) != 0) show_error_message(err);
            else {
                char msg[256]; snprintf(msg, sizeof(msg), "Database created: %s", name); show_error_message(msg);
                // Optional sync: if a table is in memory, offer to connect and save it
                if (table_has_schema(table)) {
                    const char *opts[] = { "Yes", "No" };
                    int pick = 0;
                    draw_list_modal("Connect and save current table?", opts, 2, &pick);
                    if (pick == 0) {
                        char cwd[512]; getcwd(cwd, sizeof(cwd));
                        char path[1024]; snprintf(path, sizeof(path), "%s/databases/%s", cwd, name);
                        DbManager *cur = db_get_active();
                        if (cur) { db_close(cur); db_set_active(NULL); }
                        DbManager *conn = db_open(path, err, sizeof(err));
                        if (!conn) show_error_message(err);
                        else {
                            db_set_active(conn);
                            char serr[256] = {0};
                            if (db_save_table(conn, table, serr, sizeof(serr)) != 0) {
                                show_error_message(serr[0] ? serr : "Save failed");
                            } else {
                                show_error_message("Saved current table to new database.");
                            }
                        }
                    }
                }
            }
        } else if (strcmp(menu[selected], "Delete DB") == 0) {
            char **dbs = NULL; int n = 0;
            if (db_list_databases(&dbs, &n, err, sizeof(err)) != 0) { show_error_message(err); continue; }
            if (n == 0) { show_error_message("No databases found."); free(dbs); continue; }
            const char **items = (const char**)dbs; int pick = 0;
            draw_list_modal("Select a database to delete", items, n, &pick);
            if (pick >= 0) {
                if (db_delete_database(dbs[pick], err, sizeof(err)) != 0) show_error_message(err);
                else show_error_message("Database deleted.");
                // If connected to this DB, close it
                DbManager *cur = db_get_active();
                if (cur && db_current_path(cur)) {
                    char cwd[512], full[1024]; getcwd(cwd, sizeof(cwd)); snprintf(full, sizeof(full), "%s/databases/%s", cwd, dbs[pick]);
                    if (strcmp(db_current_path(cur), full) == 0) { db_close(cur); db_set_active(NULL); }
                }
            }
            free_string_list(dbs, n);
        } else if (strcmp(menu[selected], "Load Table") == 0) {
            DbManager *cur = db_get_active();
            if (!cur || !db_is_connected(cur)) { show_error_message("No database connected."); continue; }
            char err[256] = {0};
            char **tables = NULL; int tcount = 0;
            if (db_list_tables(cur, &tables, &tcount, err, sizeof(err)) != 0 || tcount == 0) {
                show_error_message("No tables to load.");
                free_string_list(tables, tcount);
                continue;
            }
            const char **items = (const char**)tables; int pick = 0;
            draw_list_modal("Select table to load", items, tcount, &pick);
            if (pick >= 0) {
                if (low_ram_mode) {
                    // Low-RAM seek-only view over selected table
                    const char *db_path = db_current_path(cur);
                    int page = 200; // initial default; UI will recompute per screen
                    if (seek_mode_open_for_table(db_path, tables[pick], table, page, err, sizeof(err)) != 0) {
                        show_error_message(err[0] ? err : "Seek view failed");
                    } else {
                        show_error_message("Loaded table in Low-RAM view.");
                    }
                } else {
                    Table *loaded = db_load_table(cur, tables[pick], err, sizeof(err));
                    if (!loaded) { show_error_message(err[0] ? err : "Load failed"); }
                    else if (table) {
                        replace_table_contents(table, loaded);
                        workspace_manual_save(table, NULL, 0);
                        db_autosave_table(table, err, sizeof(err));
                    }
                }
            }
            free_string_list(tables, tcount);
        } else if (strcmp(menu[selected], "Delete Table") == 0) {
            DbManager *cur = db_get_active();
            if (!cur || !db_is_connected(cur)) { show_error_message("No database connected."); continue; }
            char **tables = NULL; int tcount = 0;
            if (db_list_tables(cur, &tables, &tcount, err, sizeof(err)) != 0 || tcount == 0) {
                show_error_message("No tables to delete.");
                free_string_list(tables, tcount);
                continue;
            }
            const char **items = (const char**)tables; int pick = 0;
            draw_list_modal("Select a table to delete", items, tcount, &pick);
            if (pick >= 0) {
                if (db_delete_table(cur, tables[pick], err, sizeof(err)) != 0) show_error_message(err);
                else show_error_message("Table deleted.");
            }
            free_string_list(tables, tcount);
        } else if (strcmp(menu[selected], "Close") == 0) {
            DbManager *cur = db_get_active();
            if (cur) { db_close(cur); db_set_active(NULL); show_error_message("Database connection closed."); }
            else show_error_message("No database connected.");
        }
    }
}
