/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* CSV import and export entry points. */

#ifndef CSV_H
#define CSV_H

#include <stddef.h>
#include <stdbool.h>
#include "data/table.h"
#include "core/progress.h"

/* Load CSV data into a new table, optionally inferring column types. */
Table *csv_load(const char *path, bool infer_types, char *err, size_t err_sz);
Table *csv_load_with_progress(const char *path,
                              bool infer_types,
                              char *err,
                              size_t err_sz,
                              const ProgressReporter *progress);

/* Save the provided table to a CSV file. */
int csv_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif // CSV_H
