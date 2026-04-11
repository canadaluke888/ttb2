#ifndef CSV_H
#define CSV_H

#include <stddef.h>
#include <stdbool.h>
#include "table.h"
#include "progress.h"

// Load a CSV file into a new Table.
// If infer_types is true, column types are inferred from data; otherwise all are TYPE_STR.
// On success returns allocated Table*; on failure returns NULL and fills err.
Table *csv_load(const char *path, bool infer_types, char *err, size_t err_sz);
Table *csv_load_with_progress(const char *path,
                              bool infer_types,
                              char *err,
                              size_t err_sz,
                              const ProgressReporter *progress);

// Save a Table to CSV at path. Returns 0 on success.
int csv_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif // CSV_H
