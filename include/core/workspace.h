/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Workspace lifecycle and autosave state management APIs. */

#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>
#include "data/table.h"
#include "core/progress.h"

/* Initialize the workspace and return the active table if one is available. */
int workspace_init(Table **out_table, char *err, size_t err_sz);
/* Write the active workspace book to its current autosave location. */
int workspace_autosave(const Table *table, char *err, size_t err_sz);
/* Queue an autosave to run after a short idle period instead of blocking the UI. */
void workspace_queue_autosave(Table *table);
/* Flush a queued autosave when its idle deadline has been reached. */
int workspace_process_autosave(char *err, size_t err_sz);
/* Force any queued autosave to be written immediately. */
int workspace_flush_autosave(char *err, size_t err_sz);
/* Persist the active workspace to its project path on explicit user request. */
int workspace_manual_save(const Table *table, char *err, size_t err_sz);
int workspace_manual_save_with_progress(const Table *table, const ProgressReporter *progress, char *err, size_t err_sz);
/* Open a workspace book into the current in-memory table. */
int workspace_open_book(Table *table, const char *path, char *err, size_t err_sz);
/* Switch the active table within the currently opened workspace book. */
int workspace_switch_table(Table *table, const char *table_id, char *err, size_t err_sz);
/* Add a new table to the active workspace book and switch focus to it. */
int workspace_new_table(Table *table, char *err, size_t err_sz);
/* Rename a table stored inside the active workspace book. */
int workspace_rename_table(Table *table, const char *table_id, const char *name, char *err, size_t err_sz);
/* Delete a table from the active workspace book and retarget the active table. */
int workspace_delete_table(Table *table, const char *table_id, char *err, size_t err_sz);
/* Export the active workspace to the legacy directory-based book format. */
int workspace_export_book(const char *path, char *err, size_t err_sz);
/* Export the active workspace to the SQLite-backed book format. */
int workspace_export_book_db(const char *path, char *err, size_t err_sz);
/* Query metadata about the active workspace book. */
int workspace_list_book_tables(char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz);
int workspace_book_is_active(void);
const char *workspace_book_name(void);
int workspace_rename_book(const char *name, char *err, size_t err_sz);
const char *workspace_active_table_id(void);
void workspace_clear_book(void);
/* Runtime toggles and active-project bookkeeping. */
void workspace_set_autosave_enabled(int enabled);
int workspace_autosave_enabled(void);
const char *workspace_project_path(void);
int workspace_set_project_path(const char *path);
/* Point workspace-global helpers at the table instance owned by the UI. */
void workspace_set_active_table(Table *table);
/* Release workspace-owned resources and clear process-global state. */
void workspace_shutdown(void);

#endif /* WORKSPACE_H */
