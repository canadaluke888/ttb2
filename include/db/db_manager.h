/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Database manager APIs for opening, saving, and exporting SQLite tables. */

#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <stddef.h>
#include "data/table.h"

/* Opaque manager for SQLite database connections. */
typedef struct DbManager DbManager;

/* Lifecycle helpers for direct database connections. */
DbManager *db_open(const char *path, char *err, size_t err_sz);   // open existing DB
/* Close an opened database connection. */
void       db_close(DbManager *db);
/* Report whether the manager currently holds a live SQLite connection. */
int        db_is_connected(DbManager *db);
/* Return the path backing the current database connection, if any. */
const char*db_current_path(DbManager *db);

/* Global active connection accessors used by the workspace layer. */
void       db_set_active(DbManager *db);
DbManager* db_get_active(void);
int        db_autosave_enabled(void);
void       db_set_autosave_enabled(int enabled);
/* Mirror the current in-memory table into the active database if autosave is enabled. */
int        db_autosave_table(const Table *t, char *err, size_t err_sz);

/* Filesystem helpers for the local databases/ directory. */
int  db_ensure_databases_dir(char *err, size_t err_sz);
int  db_create_database(const char *name, char *err, size_t err_sz); // creates databases/<name>
int  db_delete_database(const char *name, char *err, size_t err_sz);
int  db_list_databases(char ***names_out, int *count_out, char *err, size_t err_sz); // caller frees array and strings

/* Introspection and table-level operations on the active database. */
int  db_list_tables(DbManager *db, char ***names_out, int *count_out, char *err, size_t err_sz); // caller frees
/* Delete a single table from the connected database. */
int  db_delete_table(DbManager *db, const char *name, char *err, size_t err_sz);
/* Check whether a table name already exists in the connected database. */
int  db_table_exists(DbManager *db, const char *name);

/* Load a SQLite table into a newly allocated in-memory table. */
Table* db_load_table(DbManager *db, const char *name, char *err, size_t err_sz);

/* Persist an in-memory table or workspace out to disk. */
int  db_save_table(DbManager *db, const Table *t, char *err, size_t err_sz);
/* Export the current table to a standalone SQLite file. */
int  db_export_table_path(const Table *t, const char *path, char *err, size_t err_sz);
/* Export a workspace book database to a new destination path. */
int  db_export_book_path(const char *book_path, const char *path, char *err, size_t err_sz);

/* Search behavior stays in the UI layer; no DB search API is exposed here. */

#endif // DB_MANAGER_H
