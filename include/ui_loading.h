#ifndef UI_LOADING_H
#define UI_LOADING_H

#include "progress.h"

typedef struct UiLoadingModal UiLoadingModal;

UiLoadingModal *ui_loading_modal_start(const char *title,
                                       const char *initial_message,
                                       ProgressReporter *out_reporter);

void ui_loading_modal_update(UiLoadingModal *modal,
                             double progress,
                             const char *message);

void ui_loading_modal_finish(UiLoadingModal *modal);

#endif /* UI_LOADING_H */
