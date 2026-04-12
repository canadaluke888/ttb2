/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Progress callback types shared by long-running operations. */

#ifndef PROGRESS_H
#define PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Receives normalized progress updates and an optional status message. */
typedef void (*ProgressUpdateFn)(void *ctx, double progress, const char *message);

/* Bundles a progress callback with its caller-owned context pointer. */
typedef struct {
    ProgressUpdateFn update;
    void *ctx;
} ProgressReporter;

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_H */
