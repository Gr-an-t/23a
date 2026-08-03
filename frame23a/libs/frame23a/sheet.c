#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame23a/fsutil.h"
#include "frame23a/sheet.h"

char *sheet_filter_escape(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);

    for (const char *p = s; *p; p++) {
        if (strchr("\\':[],;", *p)) strbuf_puts(&sb, "\\");
        strbuf_append(&sb, p, 1);
    }

    return sb.data ? sb.data : fs_strdup("");
}

int sheet_run_ffmpeg(const sheet_ctx_t *ctx, argv_t *a, const char *what) {
    if (ctx->verbose) {
        char *line = proc_argv_display(a);
        fprintf(stderr, "  $ %s\n", line);
        free(line);
    }

    proc_result_t res;
    int ok = proc_run_argv(a, PROC_CAPTURE_ERR, &res);

    if (!ok && !ctx->quiet) {
        fprintf(stderr, "error: ffmpeg failed (%s)\n", what);
        if (res.err && *res.err) {
            /* ffmpeg's own diagnostic is far more useful than an exit code. */
            fprintf(stderr, "%s", res.err);
        }
    }

    proc_result_free(&res);
    return ok;
}

int sheet_render_header(const sheet_ctx_t *ctx, const textbuf_t *tb, int width,
                        const char *out_png, int *out_height) {
    header_t h = layout_header(tb->lines, width);

    char *textpath = fs_path_join(ctx->tmpdir, "header.txt");
    if (!textpath) return 0;

    if (!textbuf_write(tb, textpath)) {
        fprintf(stderr, "error: cannot write header text: %s\n", textpath);
        free(textpath);
        return 0;
    }

    char *esc_font = sheet_filter_escape(ctx->tools->font);
    char *esc_text = sheet_filter_escape(textpath);

    char size[64];
    snprintf(size, sizeof(size), "color=c=" SHEET_BG ":s=%dx%d", width, h.height);

    char filter[2048];
    snprintf(filter, sizeof(filter),
             "drawtext=fontfile=%s:textfile=%s:expansion=none"
             ":x=%d:y=%d:fontsize=%d:fontcolor=" SHEET_FG ":line_spacing=%d",
             esc_font, esc_text, h.pad_x, h.pad_y, h.font_size, h.line_spacing);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-f");
    argv_add(&a, "lavfi");
    argv_add(&a, "-i");
    argv_add(&a, size);
    argv_add(&a, "-vf");
    argv_add(&a, filter);
    argv_add(&a, "-frames:v");
    argv_add(&a, "1");
    argv_add(&a, "-y");
    argv_add(&a, out_png);

    int ok = sheet_run_ffmpeg(ctx, &a, "render header");

    if (ok && out_height) *out_height = h.height;

    free(esc_font);
    free(esc_text);
    free(textpath);
    return ok;
}

int sheet_stack(const sheet_ctx_t *ctx, const char *top_png, const char *bottom_png,
                const char *out_png) {
    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-i");
    argv_add(&a, top_png);
    argv_add(&a, "-i");
    argv_add(&a, bottom_png);
    argv_add(&a, "-filter_complex");
    argv_add(&a, "vstack=inputs=2");
    argv_add(&a, "-frames:v");
    argv_add(&a, "1");
    argv_add(&a, "-y");
    argv_add(&a, out_png);

    return sheet_run_ffmpeg(ctx, &a, "compose sheet");
}
