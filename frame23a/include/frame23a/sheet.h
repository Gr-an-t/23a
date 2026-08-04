#ifndef FRAME23A_SHEET_H
#define FRAME23A_SHEET_H

#include "frame23a/layout.h"
#include "frame23a/proc.h"
#include "frame23a/scan.h"
#include "frame23a/textbuf.h"
#include "frame23a/tools.h"

#define SHEET_BG "#181818"
#define SHEET_TILE_BG "#101010"
#define SHEET_FG "#e8e8e8"

/* A one-second loop at a delay GIF can represent exactly (10 centiseconds). */
#define SHEET_GIF_FRAMES 10
#define SHEET_GIF_FPS 10
#define SHEET_GIF_MAX_FRAMES 60

typedef struct {
    const tools_t *tools;
    const char *tmpdir;

    int width;
    int columns;
    int count;
    int min_tile;
    int per_page;
    int no_timestamps;
    int jobs;

    /* Video sheets only: tiles become short looping clips instead of stills. */
    int gif;
    int gif_frames;
    int gif_fps;

    int dry_run;
    int verbose;
    int quiet;
} sheet_ctx_t;

/*
 * Backslash-escapes the characters ffmpeg treats as filtergraph or filter-arg
 * separators. Required for any path interpolated into a -vf string; paths
 * passed as their own argv element need no escaping.
 */
char *sheet_filter_escape(const char *s);

int sheet_run_ffmpeg(const sheet_ctx_t *ctx, argv_t *a, const char *what);

/* Renders the header band, sized from the line count in `tb`. */
int sheet_render_header(const sheet_ctx_t *ctx, const textbuf_t *tb, int width,
                        const char *out_png, int *out_height);

int sheet_stack(const sheet_ctx_t *ctx, const char *top_png, const char *bottom_png,
                const char *out_png);

/* Writes a PNG, or an animated GIF when ctx->gif is set; the caller picks the
 * extension to match. */
int sheet_video_build(const sheet_ctx_t *ctx, const char *src, const char *out_path);
/* `label` names the output file; it is the group's own label unless the caller
 * had to disambiguate it against another group in the same run. */
int sheet_image_build(const sheet_ctx_t *ctx, const image_group_t *group, const char *out_dir,
                      const char *label, int *sheets_written);

#endif
