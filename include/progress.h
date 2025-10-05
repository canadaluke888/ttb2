#ifndef PROGRESS_H
#define PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ProgressUpdateFn)(void *ctx, double progress, const char *message);

typedef struct {
    ProgressUpdateFn update;
    void *ctx;
} ProgressReporter;

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_H */
