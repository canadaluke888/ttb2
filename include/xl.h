#ifndef XL_H
#define XL_H

#include <stddef.h>
#include <stdbool.h>
#include "tablecraft.h"

Table *xl_load(const char *path, bool infer_types, char *err, size_t err_sz);
int xl_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif /* XL_H */
