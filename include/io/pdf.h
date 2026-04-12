/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* PDF export entry points for rendered table snapshots. */

#ifndef PDF_H
#define PDF_H

#include <stddef.h>
#include "data/table.h"

/* Render the provided table to a PDF file. */
int pdf_save(const Table *table, const char *path, char *err, size_t err_sz);

#endif /* PDF_H */
