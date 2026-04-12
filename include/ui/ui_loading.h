/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Loading modal helpers for long-running UI operations. */

#ifndef UI_LOADING_H
#define UI_LOADING_H

#include "core/progress.h"

/* Opaque modal used to display progress during long-running tasks. */
typedef struct UiLoadingModal UiLoadingModal;

/* Open, update, and close the shared loading modal. */
UiLoadingModal *ui_loading_modal_start(const char *title,
                                       const char *initial_message,
                                       ProgressReporter *out_reporter);

/* Push a new progress value and status message into the active modal. */
void ui_loading_modal_update(UiLoadingModal *modal,
                             double progress,
                             const char *message);

/* Close the loading modal and release its window resources. */
void ui_loading_modal_finish(UiLoadingModal *modal);

#endif /* UI_LOADING_H */
