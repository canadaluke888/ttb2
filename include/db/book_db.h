#ifndef BOOK_DB_H
#define BOOK_DB_H

#include <stddef.h>
#include "data/table.h"

typedef struct BookDB BookDB;

BookDB *bookdb_open(const char *path, int create_if_missing, char *err, size_t err_sz);
void bookdb_close(BookDB *db);

int bookdb_init_schema(BookDB *db, char *err, size_t err_sz);

int bookdb_create_empty(const char *path, const char *book_name, char *err, size_t err_sz);

int bookdb_get_book_name(BookDB *db, char *out, size_t out_sz, char *err, size_t err_sz);
int bookdb_set_book_name(BookDB *db, const char *name, char *err, size_t err_sz);

int bookdb_get_active_table_id(BookDB *db, char *out, size_t out_sz, char *err, size_t err_sz);
int bookdb_set_active_table_id(BookDB *db, const char *table_id, char *err, size_t err_sz);

int bookdb_list_tables(BookDB *db, char ***names_out, char ***ids_out, int *count_out, char *err, size_t err_sz);

int bookdb_save_table(BookDB *db, const char *table_id, const Table *table, char *err, size_t err_sz);
Table *bookdb_load_table(BookDB *db, const char *table_id, char *err, size_t err_sz);

int bookdb_create_table(BookDB *db, const Table *table, char *out_table_id, size_t out_sz, char *err, size_t err_sz);
int bookdb_delete_table(BookDB *db, const char *table_id, char *err, size_t err_sz);
int bookdb_rename_table(BookDB *db, const char *table_id, const char *name, char *err, size_t err_sz);

int bookdb_export_legacy_ttbx(BookDB *db, const char *dest_path, char *err, size_t err_sz);
int bookdb_import_legacy_ttbx(const char *legacy_path, const char *dest_db_path, char *err, size_t err_sz);

#endif
