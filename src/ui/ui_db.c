#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "panel_manager.h"
#include "errors.h"
#include "db_manager.h"

// Active DB connection managed via db_manager singleton helpers

static void count_cb(const char *table, int row_idx, const char *col_name, const char *col_type, const char *value, void *user) {
    (void)table; (void)row_idx; (void)col_name; (void)col_type; (void)value;
    int *p = (int*)user; if (p) (*p)++;
}

typedef struct {
    char *table;
    int   row_idx;
    char *col_name;
    char *col_type;
    char *value;
} SearchResult;

static void free_results(SearchResult *arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; ++i) {
        free(arr[i].table);
        free(arr[i].col_name);
        free(arr[i].col_type);
        free(arr[i].value);
    }
    free(arr);
}

static void collect_cb(const char *table, int row_idx, const char *col_name, const char *col_type, const char *value, void *user) {
    (void)row_idx; // keep value
    struct { SearchResult **arr; int *count; int *cap; } *ctx = user;
    if (*(ctx->count) == *(ctx->cap)) {
        *(ctx->cap) = *(ctx->cap) ? *(ctx->cap) * 2 : 16;
        *(ctx->arr) = realloc(*(ctx->arr), sizeof(SearchResult) * (*(ctx->cap)));
    }
    SearchResult *r = &(*(ctx->arr))[(*(ctx->count))++];
    r->table = strdup(table ? table : "");
    r->row_idx = row_idx;
    r->col_name = strdup(col_name ? col_name : "");
    r->col_type = strdup(col_type ? col_type : "");
    r->value = strdup(value ? value : "");
}

static void show_results_modal(const char *query, SearchResult *arr, int count) {
    if (!arr || count <= 0) { show_error_message("No matches found."); return; }
    int idx = 0; int ch;
    int w = COLS - 10; if (w < 30) w = COLS - 2;
    int h = 9; int y = (LINES - h) / 2; int x = (COLS - w) / 2;
    PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
    PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
    keypad(modal->win, TRUE);
    while (1) {
        werase(modal->win);
        box(modal->win, 0, 0);
        // Title
        wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
        mvwprintw(modal->win, 1, 2, "Search Results");
        wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
        // Pager
        wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
        mvwprintw(modal->win, 1, w - 10, "%d/%d", idx + 1, count);
        wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        // Query line
        wattron(modal->win, COLOR_PAIR(3));
        mvwprintw(modal->win, 2, 2, "Query:");
        wattroff(modal->win, COLOR_PAIR(3));
        wattron(modal->win, COLOR_PAIR(4));
        mvwprintw(modal->win, 2, 10, "%s", query);
        wattroff(modal->win, COLOR_PAIR(4));
        // Table
        wattron(modal->win, COLOR_PAIR(3));
        mvwprintw(modal->win, 3, 2, "Table:");
        wattroff(modal->win, COLOR_PAIR(3));
        wattron(modal->win, COLOR_PAIR(4));
        mvwprintw(modal->win, 3, 10, "%s", arr[idx].table);
        wattroff(modal->win, COLOR_PAIR(4));
        // Row
        wattron(modal->win, COLOR_PAIR(3));
        mvwprintw(modal->win, 4, 2, "Row:");
        wattroff(modal->win, COLOR_PAIR(3));
        wattron(modal->win, COLOR_PAIR(4));
        mvwprintw(modal->win, 4, 10, "%d", arr[idx].row_idx);
        wattroff(modal->win, COLOR_PAIR(4));
        // Column
        wattron(modal->win, COLOR_PAIR(3));
        mvwprintw(modal->win, 5, 2, "Column:");
        wattroff(modal->win, COLOR_PAIR(3));
        wattron(modal->win, COLOR_PAIR(4));
        mvwprintw(modal->win, 5, 10, "%s (%s)", arr[idx].col_name, arr[idx].col_type);
        wattroff(modal->win, COLOR_PAIR(4));
        // Value
        wattron(modal->win, COLOR_PAIR(3));
        mvwprintw(modal->win, 6, 2, "Value:");
        wattroff(modal->win, COLOR_PAIR(3));
        wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
        mvwprintw(modal->win, 6, 10, "%s", arr[idx].value);
        wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
        // Controls
        wattron(modal->win, COLOR_PAIR(5));
        mvwprintw(modal->win, 7, 2, "[←][→] Prev/Next   [Esc] Close");
        wattroff(modal->win, COLOR_PAIR(5));
        pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
        ch = wgetch(modal->win);
        if (ch == KEY_LEFT) idx = (idx > 0) ? idx - 1 : count - 1;
        else if (ch == KEY_RIGHT) idx = (idx + 1) % count;
        else if (ch == 27 || ch == '\n') break;
    }
    pm_remove(modal); pm_remove(shadow); pm_update();
}

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
    return (int)strlen(out);
}

static void free_string_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; ++i) free(list[i]);
    free(list);
}

void show_db_manager(Table *table) {
    noecho(); curs_set(0);

    const char *menu[] = { "Connect", "Create", "Delete DB", "Load Table", "Delete Table", "Close", "Search", "Back" };
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
                    char msg[512]; snprintf(msg, sizeof(msg), "Connected to: %s", path);
                    show_error_message(msg);
                    // Sync current in-memory table into DB on connect
                    if (table) {
                        char serr[256] = {0};
                        db_save_table(conn, table, serr, sizeof(serr));
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
        } else if (strcmp(menu[selected], "Search") == 0) {
            DbManager *cur = db_get_active();
            if (!cur || !db_is_connected(cur)) { show_error_message("No database connected."); continue; }
            char query[128] = {0};
            if (prompt_text_input("Search", "Query: ", query, sizeof(query)) <= 0) continue;
            // Choose table or All
            char **tables = NULL; int tcount = 0;
            if (db_list_tables(cur, &tables, &tcount, err, sizeof(err)) != 0) { show_error_message(err); continue; }
            int count = tcount + 1; char **labels = (char**)malloc(sizeof(char*) * count);
            labels[0] = strdup("All Tables");
            for (int i = 0; i < tcount; ++i) labels[i+1] = strdup(tables[i]);
            const char **items = (const char**)labels; int pick = 0;
            // clear any prior query prompt artifacts
            clear(); refresh();
            draw_list_modal("Search in table", items, count, &pick);
            int matches = 0; SearchResult *res = NULL; int cap = 0;
            if (pick >= 0) {
                const char *tbl = (pick == 0) ? NULL : labels[pick];
                struct { SearchResult **arr; int *count; int *cap; } ctx = { &res, &matches, &cap };
                if (db_search(cur, query, tbl, collect_cb, &ctx, err, sizeof(err)) != 0) {
                    show_error_message(err);
                } else {
                    show_results_modal(query, res, matches);
                }
            }
            free_results(res, matches);
            for (int i = 0; i < count; ++i) free(labels[i]);
            free(labels);
            free_string_list(tables, tcount);
        }
    }
}
