#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>
#include "tablecraft.h"

int workspace_init(Table **out_table, char *err, size_t err_sz);
int workspace_autosave(const Table *table, char *err, size_t err_sz);
int workspace_manual_save(const Table *table, char *err, size_t err_sz);
int workspace_open_book(Table *table, const char *path, char *err, size_t err_sz);
int workspace_switch_table(Table *table, const char *table_id, char *err, size_t err_sz);
int workspace_new_table(Table *table, char *err, size_t err_sz);
int workspace_export_book(const char *path, char *err, size_t err_sz);
int workspace_list_book_tables(char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz);
int workspace_book_is_active(void);
const char *workspace_book_name(void);
int workspace_rename_book(const char *name, char *err, size_t err_sz);
const char *workspace_active_table_id(void);
void workspace_clear_book(void);
void workspace_set_autosave_enabled(int enabled);
int workspace_autosave_enabled(void);
const char *workspace_project_path(void);
int workspace_set_project_path(const char *path);
void workspace_set_active_table(Table *table);
void workspace_shutdown(void);

#endif /* WORKSPACE_H */
