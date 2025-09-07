#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "panel_manager.h"
#include "errors.h"
#include "db_manager.h"
#include "ui.h"

// Active DB connection managed via db_manager singleton helpers

static void draw_list_modal(const char *title, const char **items, int count, int *io_selected) {
    int h = (count + 3);
    if (h < 7) h = 7;
    int w = COLS - 4;
    int y = (LINES - h) / 2;
    int x = 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);

    int selected = (io_selected && *io_selected >= 0 && *io_selected < count) ? *io_selected : 0;
    int ch;
    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "%s", title ? title : "");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        for (int i = 0; i < count; ++i) {
            if (i == selected) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(modal->win, 2 + i, 2, "%s", items[i]);
            if (i == selected) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        }
        pm_wnoutrefresh(shadow);
        pm_wnoutrefresh(modal);
        pm_update();

        ch = wgetch(modal->win);
        if (ch == KEY_UP) selected = (selected > 0) ? selected - 1 : count - 1;
        else if (ch == KEY_DOWN) selected = (selected + 1) % count;
        else if (ch == '\n') break;
        else if (ch == 27) { selected = -1; break; }
    }
    if (io_selected) *io_selected = selected;
    pm_remove(modal); pm_remove(shadow); pm_update();
}

static int prompt_text_input(const char *title, const char *prompt, char *out, size_t out_sz) {
    echo(); curs_set(1);
    nodelay(stdscr, FALSE);
    int w = COLS - 4; int x = 2; int y = LINES / 2 - 2;
    for (int i = 0; i < 5; ++i) { move(y + i, 0); clrtoeol(); }
    mvaddch(y, x, ACS_ULCORNER); for (int i = 1; i < w - 1; ++i) mvaddch(y, x + i, ACS_HLINE); mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + 1, x, ACS_VLINE);
    if (title) { attron(COLOR_PAIR(3) | A_BOLD); mvprintw(y + 1, x + 1, " %s", title); attroff(COLOR_PAIR(3) | A_BOLD); }
    mvaddch(y + 1, x + w - 1, ACS_VLINE);
    mvaddch(y + 2, x, ACS_VLINE); attron(COLOR_PAIR(4)); mvprintw(y + 2, x + 1, " %s", prompt); attroff(COLOR_PAIR(4)); mvaddch(y + 2, x + w - 1, ACS_VLINE);
    mvaddch(y + 3, x, ACS_LLCORNER); for (int i = 1; i < w - 1; ++i) mvaddch(y + 3, x + i, ACS_HLINE); mvaddch(y + 3, x + w - 1, ACS_LRCORNER);
    move(y + 2, x + 3 + (int)strlen(prompt));
    getnstr(out, (int)out_sz - 1);
    noecho(); curs_set(0);
    nodelay(stdscr, TRUE);
    return (int)strlen(out);
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
                                char serr[256] = {0};
                                if (db_save_table(conn, table, serr, sizeof(serr)) != 0) {
                                    show_error_message(serr[0] ? serr : "Save failed");
                                } else {
                                    show_error_message("Database updated from memory table.");
                                }
                            } else if (pick == 1) {
                                char lerr[256] = {0};
                                Table *loaded = db_load_table(conn, table->name, lerr, sizeof(lerr));
                                if (!loaded) {
                                    show_error_message(lerr[0] ? lerr : "Load failed");
                                } else {
                                    if (table->name) free(table->name);
                                    for (int i = 0; i < table->column_count; i++) { if (table->columns[i].name) free(table->columns[i].name); }
                                    free(table->columns);
                                    for (int i = 0; i < table->row_count; i++) {
                                        if (table->rows[i].values) {
                                            for (int j = 0; j < table->column_count; j++) { if (table->rows[i].values[j]) free(table->rows[i].values[j]); }
                                            free(table->rows[i].values);
                                        }
                                    }
                                    free(table->rows);
                                    table->name = loaded->name;
                                    table->columns = loaded->columns;
                                    table->column_count = loaded->column_count;
                                    table->rows = loaded->rows;
                                    table->row_count = loaded->row_count;
                                    table->capacity_columns = loaded->capacity_columns;
                                    table->capacity_rows = loaded->capacity_rows;
                                    free(loaded);
                                    show_error_message("Memory table replaced from database.");
                                }
                            } else {
                                // Skip
                            }
                        } else {
                            // No conflict: save current in-memory table into DB
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
                if (table) {
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
                        if (table->name) free(table->name);
                        for (int i = 0; i < table->column_count; i++) { if (table->columns[i].name) free(table->columns[i].name); }
                        free(table->columns);
                        for (int i = 0; i < table->row_count; i++) {
                            if (table->rows[i].values) {
                                for (int j = 0; j < table->column_count; j++) { if (table->rows[i].values[j]) free(table->rows[i].values[j]); }
                                free(table->rows[i].values);
                            }
                        }
                        free(table->rows);
                        table->name = loaded->name;
                        table->columns = loaded->columns;
                        table->column_count = loaded->column_count;
                        table->rows = loaded->rows;
                        table->row_count = loaded->row_count;
                        table->capacity_columns = loaded->capacity_columns;
                        table->capacity_rows = loaded->capacity_rows;
                        free(loaded);
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
