#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <stddef.h>
#include "tablecraft.h"

// Opaque manager for SQLite database connections
typedef struct DbManager DbManager;

// Lifecycle
DbManager *db_open(const char *path, char *err, size_t err_sz);   // open existing DB
void       db_close(DbManager *db);
int        db_is_connected(DbManager *db);
const char*db_current_path(DbManager *db);

// Global active connection + autosave
void       db_set_active(DbManager *db);
DbManager* db_get_active(void);
int        db_autosave_enabled(void);
void       db_set_autosave_enabled(int enabled);
int        db_autosave_table(const Table *t, char *err, size_t err_sz);

// Filesystem helpers (databases/ directory within CWD)
int  db_ensure_databases_dir(char *err, size_t err_sz);
int  db_create_database(const char *name, char *err, size_t err_sz); // creates databases/<name>
int  db_delete_database(const char *name, char *err, size_t err_sz);
int  db_list_databases(char ***names_out, int *count_out, char *err, size_t err_sz); // caller frees array and strings

// Introspection
int  db_list_tables(DbManager *db, char ***names_out, int *count_out, char *err, size_t err_sz); // caller frees
int  db_delete_table(DbManager *db, const char *name, char *err, size_t err_sz);

// Load table from SQLite into a new Table (caller frees via free_table)
Table* db_load_table(DbManager *db, const char *name, char *err, size_t err_sz);

// Persist current Table into SQLite (drops/recreates table to match schema)
int  db_save_table(DbManager *db, const Table *t, char *err, size_t err_sz);

// Search API: calls callback once per match. If table_name==NULL, searches all tables.
typedef void (*db_search_cb)(const char *table,
                             int row_index,
                             const char *column_name,
                             const char *column_type,
                             const char *value,
                             void *user);
int  db_search(DbManager *db, const char *query, const char *table_name,
               db_search_cb on_match, void *user, char *err, size_t err_sz);

#endif // DB_MANAGER_H
