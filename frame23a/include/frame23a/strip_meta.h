#ifndef FRAME23A_STRIP_META_H
#define FRAME23A_STRIP_META_H

#include "frame23a/tools.h"

typedef enum {
    STRIP_OK,
    STRIP_SKIPPED,
    STRIP_FAILED,
} strip_status_t;

typedef struct {
    const tools_t *tools;
    const char *tmpdir;
    int in_place;
    int dry_run;
    int verbose;
    int quiet;
} strip_ctx_t;

/* out_root is ignored (and may be NULL) when ctx->in_place is set. */
strip_status_t strip_file(const strip_ctx_t *ctx, const char *src, const char *out_root);

#endif
