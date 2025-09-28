#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>
#include "tablecraft.h"

int workspace_init(Table **out_table, char *err, size_t err_sz);
int workspace_autosave(const Table *table, char *err, size_t err_sz);
int workspace_manual_save(const Table *table, char *err, size_t err_sz);
void workspace_set_autosave_enabled(int enabled);
int workspace_autosave_enabled(void);
const char *workspace_project_path(void);
int workspace_set_project_path(const char *path);
void workspace_set_active_table(Table *table);

#endif /* WORKSPACE_H */
