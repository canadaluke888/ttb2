/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* XLSX import and export entry points. */

#ifndef XL_H
#define XL_H

#include <stddef.h>
#include <stdbool.h>
#include "data/table.h"
#include "core/progress.h"

/* Load or save XLSX data for a single worksheet-backed table. */
Table *xl_load(const char *path, bool infer_types, char *err, size_t err_sz);
/* Load XLSX data while reporting incremental progress to the UI. */
Table *xl_load_with_progress(const char *path,
                             bool infer_types,
                             char *err,
                             size_t err_sz,
                             const ProgressReporter *progress);
/* Save the provided table to a single-sheet XLSX workbook. */
int xl_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif /* XL_H */
