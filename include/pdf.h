#ifndef PDF_H
#define PDF_H

#include <stddef.h>
#include "table.h"

int pdf_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif /* PDF_H */
