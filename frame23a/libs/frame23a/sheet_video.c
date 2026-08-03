#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/probe.h"
#include "frame23a/sheet.h"

#define MAX_FRAMES 400
#define MAX_JOBS 32

/*
 * Sampling density by length. A 4-frame sheet says nothing about a feature
 * film, and a 48-frame sheet of a 5-second clip is mostly duplicates.
 */
static int default_frame_count(double duration) {
    if (duration <= 0) return 9;
    if (duration < 10) return 4;
    if (duration < 30) return 9;
    if (duration < 120) return 12;
    if (duration < 600) return 16;
    if (duration < 1800) return 24;
    if (duration < 3600) return 35;
    return 48;
}

/*
 * Spread across the whole runtime, not the opening. A 2% head/tail trim skips
 * black leader and credits, and sampling the midpoint of each slice keeps the
 * first and last frames (often blank) out of the sheet.
 */
static void sample_window(double duration, int n, int i, double *at, double *win) {
    if (duration <= 0) {
        *at = i * 2.0;
        *win = 1.0;
        return;
    }

    double skip = duration * 0.02;
    if (skip > 5.0) skip = 5.0;

    double start = skip;
    double end = duration - skip;
    if (end <= start) {
        start = 0.0;
        end = duration;
    }

    double slice = (end - start) / n;
    *at = start + (i + 0.5) * slice;
    *win = slice < 2.0 ? slice : 2.0;
}

static pid_t spawn_frame(const sheet_ctx_t *ctx, const char *src, const grid_t *g,
                         double at, double win, const char *ts_text, const char *out_png) {
    char *esc_font = sheet_filter_escape(ctx->tools->font);
    char *tspath = NULL;
    char *esc_ts = NULL;

    strbuf_t filter;
    strbuf_init(&filter);

    /* Too narrow a window leaves the thumbnail filter nothing to choose from. */
    int use_thumbnail = win >= 0.2;
    if (use_thumbnail) strbuf_printf(&filter, "thumbnail=30,");

    /* Uniform format for the same reason as image tiles: a mid-sequence
     * pixel-format change would truncate the grid. */
    strbuf_printf(&filter,
                  "scale=%d:%d:force_original_aspect_ratio=decrease,"
                  "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=" SHEET_TILE_BG ",format=rgb24",
                  g->tile_w, g->tile_h, g->tile_w, g->tile_h);

    if (ts_text) {
        /* Derived from the frame's own output name so parallel jobs cannot
         * overwrite each other's timestamp text. */
        size_t need = strlen(out_png) + 5;
        tspath = malloc(need);
        if (tspath) snprintf(tspath, need, "%s.txt", out_png);

        if (tspath && fs_write_file(tspath, ts_text, strlen(ts_text))) {
            esc_ts = sheet_filter_escape(tspath);
            strbuf_printf(&filter,
                          ",drawtext=fontfile=%s:textfile=%s:expansion=none"
                          ":x=w-tw-8:y=h-th-8:fontsize=%d:fontcolor=white"
                          ":box=1:boxcolor=black@0.55:boxborderw=5",
                          esc_font, esc_ts, layout_timestamp_font(g->tile_w));
        }
    }

    char at_s[32], win_s[32];
    snprintf(at_s, sizeof(at_s), "%.3f", at);
    snprintf(win_s, sizeof(win_s), "%.3f", win);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-ss");
    argv_add(&a, at_s);
    if (win > 0) {
        argv_add(&a, "-t");
        argv_add(&a, win_s);
    }
    argv_add(&a, "-i");
    argv_add(&a, src);
    argv_add(&a, "-an");
    argv_add(&a, "-sn");
    argv_add(&a, "-dn");
    argv_add(&a, "-vf");
    argv_add(&a, filter.data);
    argv_add(&a, "-frames:v");
    argv_add(&a, "1");
    argv_add(&a, "-y");
    argv_add(&a, out_png);

    if (ctx->verbose) {
        char *line = proc_argv_display(&a);
        fprintf(stderr, "  $ %s\n", line);
        free(line);
    }

    pid_t pid = proc_spawn(&a);

    strbuf_free(&filter);
    free(esc_font);
    free(esc_ts);
    free(tspath);
    return pid;
}

/*
 * Frames are extracted to raw_NNN.png then renumbered contiguously, because
 * ffmpeg's image2 reader stops at the first gap in a %03d sequence — one
 * failed seek would otherwise silently truncate the sheet.
 */
static int extract_all(const sheet_ctx_t *ctx, const char *src, const media_info_t *mi,
                       int n, const grid_t *g) {
    int jobs = ctx->jobs < 1 ? 1 : ctx->jobs;
    if (jobs > MAX_JOBS) jobs = MAX_JOBS;
    if (jobs > n) jobs = n;

    int active = 0;

    for (int i = 0; i < n; i++) {
        double at, win;
        sample_window(mi->duration, n, i, &at, &win);

        char name[64];
        snprintf(name, sizeof(name), "raw_%03d.png", i);
        char *raw = fs_path_join(ctx->tmpdir, name);
        if (!raw) continue;

        char ts[32];
        fs_hms_short(at, ts, sizeof(ts));

        if (active >= jobs) {
            proc_reap_one();
            active--;
        }

        if (spawn_frame(ctx, src, g, at, win, ctx->no_timestamps ? NULL : ts, raw) > 0) active++;

        free(raw);
    }

    while (active-- > 0) proc_reap_one();

    /*
     * Renumbered contiguously in chronological order after the fact, because
     * ffmpeg's image2 reader stops at the first gap in a %03d sequence — one
     * failed seek would otherwise silently truncate the sheet.
     */
    int got = 0;

    for (int i = 0; i < n; i++) {
        char name[64];
        snprintf(name, sizeof(name), "raw_%03d.png", i);
        char *raw = fs_path_join(ctx->tmpdir, name);
        if (!raw) continue;

        if (fs_file_size(raw) > 0) {
            char final[64];
            snprintf(final, sizeof(final), "f_%03d.png", got);
            char *dst = fs_path_join(ctx->tmpdir, final);

            if (dst && rename(raw, dst) == 0) got++;
            free(dst);
        } else {
            unlink(raw);
            if (!ctx->quiet) {
                double at, win;
                sample_window(mi->duration, n, i, &at, &win);

                char ts[32];
                fs_hms_short(at, ts, sizeof(ts));
                fprintf(stderr, "warning: could not extract frame at %s from %s\n", ts, src);
            }
        }

        free(raw);
    }

    return got;
}

int sheet_video_build(const sheet_ctx_t *ctx, const char *src, const char *out_png) {
    media_info_t mi = probe_file(ctx->tools, src);

    if (!mi.valid || !mi.has_video) {
        fprintf(stderr, "warning: no video stream, skipping: %s\n", src);
        return 0;
    }

    int n = ctx->count > 0 ? ctx->count : default_frame_count(mi.duration);
    if (n > MAX_FRAMES) {
        fprintf(stderr, "warning: capping frame count at %d\n", MAX_FRAMES);
        n = MAX_FRAMES;
    }

    int min_tile = ctx->min_tile > 0 ? ctx->min_tile : LAYOUT_MIN_TILE_VIDEO;
    grid_t g = layout_grid(n, mi.width, mi.height, ctx->width, min_tile, ctx->columns);

    if (ctx->dry_run) {
        printf("would write %s  (%d frames, %dx%d grid, %dx%d tiles)\n", out_png, n, g.cols,
               g.rows, g.tile_w, g.tile_h);
        return 1;
    }

    if (!ctx->quiet) printf("%s\n", out_png);

    int got = extract_all(ctx, src, &mi, n, &g);
    if (got == 0) {
        fprintf(stderr, "error: no frames could be extracted from %s\n", src);
        return 0;
    }

    if (got < n && !ctx->quiet) {
        fprintf(stderr, "warning: extracted %d of %d frames from %s\n", got, n, src);
    }

    layout_set_count(&g, got);

    char *pattern = fs_path_join(ctx->tmpdir, "f_%03d.png");
    char *grid_png = fs_path_join(ctx->tmpdir, "grid.png");
    char *header_png = fs_path_join(ctx->tmpdir, "header.png");

    int ok = 0;
    if (!pattern || !grid_png || !header_png) goto done;

    char tile[128];
    snprintf(tile, sizeof(tile), "tile=%dx%d:padding=%d:margin=%d:color=" SHEET_BG, g.cols, g.rows,
             g.padding, g.margin);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-i");
    argv_add(&a, pattern);
    argv_add(&a, "-vf");
    argv_add(&a, tile);
    argv_add(&a, "-frames:v");
    argv_add(&a, "1");
    argv_add(&a, "-y");
    argv_add(&a, grid_png);

    if (!sheet_run_ffmpeg(ctx, &a, "tile frames")) goto done;

    char *name = fs_basename_dup(src);
    textbuf_t tb;
    textbuf_init(&tb);
    textbuf_video_header(&tb, name ? name : src, &mi, got);
    free(name);

    ok = sheet_render_header(ctx, &tb, g.grid_w, header_png, NULL) &&
         sheet_stack(ctx, header_png, grid_png, out_png);

    textbuf_free(&tb);

done:
    free(pattern);
    free(grid_png);
    free(header_png);
    return ok;
}
