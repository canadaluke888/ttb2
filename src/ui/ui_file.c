#include <ncurses.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "panel_manager.h"
#include "errors.h"
#include "settings.h"
#include "csv.h"
#include "db_manager.h"
#include "ui.h"

typedef struct {
    char *name;
    int is_dir;
} Entry;

static int entry_cmp(const void *a, const void *b) {
    const Entry *ea = (const Entry*)a, *eb = (const Entry*)b;
    if (ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir; // dirs first
    return strcmp(ea->name, eb->name);
}

static Entry *list_dir(const char *path, int *out_count) {
    DIR *d = opendir(path);
    if (!d) { *out_count = 0; return NULL; }
    int cap = 32, n = 0; Entry *arr = (Entry*)malloc(sizeof(Entry) * cap);
    // Add parent entry
    arr[n].name = strdup(".."); arr[n].is_dir = 1; n++;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
        // skip hidden by default
        if (nm[0] == '.') continue;
        if (n == cap) { cap *= 2; arr = (Entry*)realloc(arr, sizeof(Entry) * cap); }
        arr[n].name = strdup(nm);
        arr[n].is_dir = (de->d_type == DT_DIR);
        n++;
    }
    closedir(d);
    qsort(arr, n, sizeof(Entry), entry_cmp);
    *out_count = n;
    return arr;
}

static void free_entries(Entry *arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; ++i) free(arr[i].name);
    free(arr);
}

void show_open_file(Table *table) {
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    int sel = 0;
    while (1) {
        int count = 0; Entry *ents = list_dir(cwd, &count);
        if (!ents) { show_error_message("Failed to read directory."); return; }
        int h = (count + 5); if (h < 10) h = 10; if (h > LINES - 2) h = LINES - 2;
        int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
        PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
        PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
        keypad(modal->win, TRUE);
        int top = 0; if (sel >= h - 4) top = sel - (h - 5);

        int ch;
        while (1) {
            werase(modal->win); box(modal->win, 0, 0);
            wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(modal->win, 1, 2, "Open File - %s", cwd);
            wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
            int visible = h - 3; if (visible < 1) visible = 1;
            for (int i = 0; i < visible && top + i < count; ++i) {
                int idx = top + i;
                if (idx == sel) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                mvwprintw(modal->win, 2 + i, 2, "%s%s", ents[idx].name, ents[idx].is_dir ? "/" : "");
                if (idx == sel) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
            }
            pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
            ch = wgetch(modal->win);
            if (ch == KEY_UP) { sel = (sel > 0) ? sel - 1 : count - 1; if (sel < top) top = sel; }
            else if (ch == KEY_DOWN) { sel = (sel + 1) % count; if (sel >= top + visible) top = sel - visible + 1; }
            else if (ch == KEY_LEFT) { sel = 0; ch = 10; /* treat as enter on '..' */ }
            else if (ch == '\n') break;
            else if (ch == 27) { pm_remove(modal); pm_remove(shadow); pm_update(); free_entries(ents, count); return; }
        }

        // Handle selection
        char path[1536];
        if (strcmp(ents[sel].name, "..") == 0) {
            // go up
            char *slash = strrchr(cwd, '/');
            if (slash && slash != cwd) { *slash = '\0'; }
            else strcpy(cwd, "/");
            pm_remove(modal); pm_remove(shadow); pm_update();
            free_entries(ents, count);
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", cwd, ents[sel].name);
        struct stat st; if (stat(path, &st) != 0) { show_error_message("Stat failed."); pm_remove(modal); pm_remove(shadow); pm_update(); free_entries(ents, count); return; }
        if (S_ISDIR(st.st_mode)) {
            // enter directory
            strncpy(cwd, path, sizeof(cwd) - 1); cwd[sizeof(cwd) - 1] = '\0'; sel = 0;
            pm_remove(modal); pm_remove(shadow); pm_update();
            free_entries(ents, count);
            continue;
        }

        // File selected
        if (!strstr(path, ".csv")) {
            show_error_message("Only CSV files are supported.");
            pm_remove(modal); pm_remove(shadow); pm_update();
            free_entries(ents, count);
            continue;
        }

        // Load settings to get type inference preference
        AppSettings s; settings_init_defaults(&s); settings_load("settings.json", &s);
        char err[256] = {0};
        Table *loaded = csv_load(path, s.type_infer_enabled, err, sizeof(err));
        if (!loaded) { show_error_message(err[0] ? err : "Failed to load CSV"); }
        else if (table) {
            // Replace current table contents with loaded
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

            // If a DB is connected and a table of same name exists, prompt to sync
            DbManager *cur = db_get_active();
            if (cur && db_is_connected(cur) && table->name && db_table_exists(cur, table->name)) {
                const char *opts[] = { "Overwrite DB", "Keep DB (no save)" };
                int pick = 0;
                // Clear screen to avoid artifacts from file modal before showing prompt
                clear(); refresh();
                // Reuse a simple list modal from DB UI by declaring here
                // Use a minimal inline modal to avoid cross-file dependency; quick prompt
                // We'll just use show_error_message for now if user chooses overwrite
                // but present a simple choice list via panels as with other modals
                // Build a tiny modal inline
                int h = 7; int w = COLS - 4; int y = (LINES - h) / 2; int x = 2;
                PmNode *shadow = pm_add(y + 1, x + 2, h, w, PM_LAYER_MODAL_SHADOW, PM_LAYER_MODAL_SHADOW);
                PmNode *modal  = pm_add(y, x, h, w, PM_LAYER_MODAL, PM_LAYER_MODAL);
                keypad(modal->win, TRUE);
                while (1) {
                    werase(modal->win); box(modal->win, 0, 0);
                    wattron(modal->win, COLOR_PAIR(3) | A_BOLD);
                    mvwprintw(modal->win, 1, 2, "Table '%s' exists in DB. Sync?", table->name);
                    wattroff(modal->win, COLOR_PAIR(3) | A_BOLD);
                    for (int i = 0; i < 2; ++i) {
                        if (i == pick) wattron(modal->win, COLOR_PAIR(4) | A_BOLD);
                        mvwprintw(modal->win, 2 + i, 2, "%s", opts[i]);
                        if (i == pick) wattroff(modal->win, COLOR_PAIR(4) | A_BOLD);
                    }
                    pm_wnoutrefresh(shadow); pm_wnoutrefresh(modal); pm_update();
                    int ch = wgetch(modal->win);
                    if (ch == KEY_UP) pick = (pick > 0) ? pick - 1 : 1;
                    else if (ch == KEY_DOWN) pick = (pick + 1) % 2;
                    else if (ch == '\n' || ch == 27) break;
                }
                pm_remove(modal); pm_remove(shadow); pm_update();
                if (pick == 0) {
                    char serr[256] = {0};
                    if (db_save_table(cur, table, serr, sizeof(serr)) != 0) show_error_message(serr[0] ? serr : "Save failed");
                    else show_error_message("Database table overwritten with loaded data.");
                }
            }
        }
        pm_remove(modal); pm_remove(shadow); pm_update();
        free_entries(ents, count);
        return;
    }
}
