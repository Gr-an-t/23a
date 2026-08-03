#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/probe.h"
#include "frame23a/proc.h"
#include "frame23a/scan.h"
#include "frame23a/scrub.h"
#include "frame23a/strip_meta.h"

static int strip_video(const strip_ctx_t *ctx, const char *src, const char *dst) {
    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-i");
    argv_add(&a, src);
    argv_add(&a, "-map");
    argv_add(&a, "0");
    argv_add(&a, "-c");
    argv_add(&a, "copy");
    argv_add(&a, "-map_metadata");
    argv_add(&a, "-1");
    argv_add(&a, "-map_chapters");
    argv_add(&a, "-1");
    /* Without this the muxer stamps its own "encoder=Lavf.." tag back on,
     * re-adding a tool fingerprint to the file we just cleaned. */
    argv_add(&a, "-fflags");
    argv_add(&a, "+bitexact");
    argv_add(&a, "-flags:v");
    argv_add(&a, "+bitexact");
    argv_add(&a, "-flags:a");
    argv_add(&a, "+bitexact");
    argv_add(&a, "-y");
    argv_add(&a, dst);

    if (ctx->verbose) {
        char *line = proc_argv_display(&a);
        fprintf(stderr, "  $ %s\n", line);
        free(line);
    }

    proc_result_t res;
    int ok = proc_run_argv(&a, PROC_CAPTURE_ERR, &res);

    if (!ok && !ctx->quiet && res.err && *res.err) fprintf(stderr, "%s", res.err);
    proc_result_free(&res);

    return ok;
}

/* Formats the native scrubber cannot parse, handed to exiftool when present. */
static int strip_with_exiftool(const strip_ctx_t *ctx, const char *src, const char *dst) {
    if (!ctx->tools->exiftool) return 0;
    if (!fs_copy_file(src, dst)) return 0;

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->exiftool);
    argv_add(&a, "-quiet");
    argv_add(&a, "-all=");
    argv_add(&a, "-tagsfromfile");
    argv_add(&a, "@");
    argv_add(&a, "-Orientation");
    argv_add(&a, "-ColorSpace");
    argv_add(&a, "-ICC_Profile");
    argv_add(&a, "-overwrite_original");
    argv_add(&a, dst);

    if (ctx->verbose) {
        char *line = proc_argv_display(&a);
        fprintf(stderr, "  $ %s\n", line);
        free(line);
    }

    proc_result_t res;
    int ok = proc_run_argv(&a, PROC_CAPTURE_ERR, &res);
    proc_result_free(&res);

    if (!ok) remove(dst);
    return ok;
}

/* An unreadable output would be worse than an untouched original, so the
 * result must decode before it is allowed to replace anything. */
static int verify_decodes(const strip_ctx_t *ctx, const char *path) {
    media_info_t mi = probe_file(ctx->tools, path);
    return mi.valid && (mi.has_video || mi.has_audio);
}

static int finish_in_place(const strip_ctx_t *ctx, const char *src, const char *tmp) {
    if (!verify_decodes(ctx, tmp)) {
        fprintf(stderr, "error: scrubbed file failed to decode, leaving original: %s\n", src);
        remove(tmp);
        return 0;
    }

    struct stat st;
    if (stat(src, &st) == 0) chmod(tmp, st.st_mode & 07777);

    if (rename(tmp, src) != 0) {
        fprintf(stderr, "error: cannot replace %s\n", src);
        remove(tmp);
        return 0;
    }

    return 1;
}

strip_status_t strip_file(const strip_ctx_t *ctx, const char *src, const char *out_root) {
    media_kind_t kind = scan_classify(src);
    if (kind == MEDIA_OTHER) return STRIP_SKIPPED;

    char *base = fs_basename_dup(src);
    if (!base) return STRIP_FAILED;

    char *dst = NULL;
    char *tmp = NULL;
    strip_status_t status = STRIP_FAILED;

    if (ctx->in_place) {
        /* The temp file sits beside the original so the replacing rename is
         * atomic and cannot fail with EXDEV across filesystems. */
        char *dir = fs_dirname_dup(src);
        char scratch[512];
        snprintf(scratch, sizeof(scratch), ".frame23a-%s", base);

        tmp = dir ? fs_path_join(dir, scratch) : NULL;
        free(dir);

        if (!tmp) goto done;
        dst = fs_strdup(tmp);
    } else {
        dst = fs_path_join(out_root, base);
    }

    if (!dst) goto done;

    if (ctx->dry_run) {
        printf("would clean %s -> %s\n", src, ctx->in_place ? src : dst);
        status = STRIP_OK;
        goto done;
    }

    if (!ctx->quiet) printf("%s\n", ctx->in_place ? src : dst);

    int ok = 0;

    if (kind == MEDIA_VIDEO) {
        ok = strip_video(ctx, src, dst);
    } else {
        switch (scrub_image(src, dst)) {
            case SCRUB_OK:
                ok = 1;
                break;

            case SCRUB_UNSUPPORTED:
                ok = strip_with_exiftool(ctx, src, dst);
                if (!ok) {
                    fprintf(stderr,
                            "warning: cannot scrub %s (only JPEG, PNG and video are handled "
                            "natively; install exiftool for other formats)\n",
                            src);
                    status = STRIP_SKIPPED;
                    goto done;
                }
                break;

            case SCRUB_ERROR:
                fprintf(stderr, "error: failed to scrub %s\n", src);
                ok = 0;
                break;
        }
    }

    if (!ok) goto done;

    if (ctx->in_place) {
        status = finish_in_place(ctx, src, tmp) ? STRIP_OK : STRIP_FAILED;
    } else {
        status = STRIP_OK;
    }

done:
    free(base);
    free(dst);
    free(tmp);
    return status;
}
