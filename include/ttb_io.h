#ifndef TTB_IO_H
#define TTB_IO_H

#include <stddef.h>
#include "tablecraft.h"

int ttbl_save(const Table *table, const char *path, char *err, size_t err_sz);
Table *ttbl_load(const char *path, char *err, size_t err_sz);

int ttbx_save(const Table *table, const char *path, char *err, size_t err_sz);
Table *ttbx_load(const char *path, char *err, size_t err_sz);

#endif /* TTB_IO_H */
