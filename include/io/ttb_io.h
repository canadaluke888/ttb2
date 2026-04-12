/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Native TTBL and legacy TTBX persistence interfaces. */

#ifndef TTB_IO_H
#define TTB_IO_H

#include <stddef.h>
#include "data/table.h"

/* Manifest entry describing one table inside a legacy TTBX directory book. */
typedef struct {
    char *id;
    char *name;
    char *file;
} TtbxTableEntry;

/* Legacy TTBX directory manifest metadata. */
typedef struct {
    char *book_name;
    char *active_table_id;
    TtbxTableEntry *tables;
    int table_count;
} TtbxManifest;

/* Native single-table TTBL serialization helpers. */
int ttbl_save(const Table *table, const char *path, char *err, size_t err_sz);
Table *ttbl_load(const char *path, char *err, size_t err_sz);

/* Legacy TTBX directory manifest and table helpers. */
void ttbx_manifest_free(TtbxManifest *manifest);
int ttbx_is_book_dir(const char *path);
int ttbx_manifest_load(const char *path, TtbxManifest *manifest, char *err, size_t err_sz);
int ttbx_manifest_save(const char *path, const TtbxManifest *manifest, char *err, size_t err_sz);
int ttbx_copy_book(const char *src_path, const char *dst_path, char *err, size_t err_sz);
int ttbx_remove_book(const char *path, char *err, size_t err_sz);
Table *ttbx_load(const char *path, char *err, size_t err_sz);
Table *ttbx_load_table(const char *path, const char *table_id, char *err, size_t err_sz);
int ttbx_rename_table(const char *path, const char *table_id, const char *name, char *err, size_t err_sz);
int ttbx_delete_table(const char *path, const char *table_id, char *next_active_id, size_t next_active_id_sz, char *err, size_t err_sz);
int ttbx_save(const Table *table, const char *path, char *err, size_t err_sz);
int ttbx_save_table(const Table *table, const char *path, const char *table_id, char *err, size_t err_sz);

#endif /* TTB_IO_H */
