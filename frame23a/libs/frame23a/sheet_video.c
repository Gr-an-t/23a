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

/*
 * Animated tiles need a run of consecutive frames rather than one
 * representative one, so the segment is centred on the sample point and read
 * at the loop's own frame rate.
 */
static void gif_segment(double duration, double at, int steps, int fps, double *seek,
                        double *len) {
    /* One frame of slack: the fps filter's first output can land just after
     * the seek point, which would otherwise cost us the last frame. */
    double span = (double)(steps + 1) / fps;

    double start = at - span / 2.0;
    if (duration > 0 && start + span > duration) start = duration - span;
    if (start < 0) start = 0;

    *seek = start;
    *len = span;
}

/*
 * `frames` > 1 makes out_path an image2 pattern (it must contain %02d) and
 * turns off the thumbnail filter: picking the single most representative frame
 * is exactly wrong when the point is to keep the motion.
 */
static pid_t spawn_sample(const sheet_ctx_t *ctx, const char *src, const grid_t *g, int index,
                          double seek, double len, const char *ts_text, const char *out_path,
                          int frames, int fps) {
    char *esc_font = sheet_filter_escape(ctx->tools->font);
    char *tspath = NULL;
    char *esc_ts = NULL;

    strbuf_t filter;
    strbuf_init(&filter);

    if (frames > 1) {
        strbuf_printf(&filter, "fps=%d,", fps);
    } else if (len >= 0.2) {
        /* Too narrow a window leaves the thumbnail filter nothing to choose
         * from. */
        strbuf_printf(&filter, "thumbnail=30,");
    }

    /* Uniform format for the same reason as image tiles: a mid-sequence
     * pixel-format change would truncate the grid. */
    strbuf_printf(&filter,
                  "scale=%d:%d:force_original_aspect_ratio=decrease,"
                  "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=" SHEET_TILE_BG ",format=rgb24",
                  g->tile_w, g->tile_h, g->tile_w, g->tile_h);

    if (ts_text) {
        /* Named after the sample, not the output, so parallel jobs cannot
         * overwrite each other's timestamp text and the name stays free of the
         * %02d an image2 pattern carries. */
        char name[32];
        snprintf(name, sizeof(name), "ts_%03d.txt", index);
        tspath = fs_path_join(ctx->tmpdir, name);

        if (tspath && fs_write_file(tspath, ts_text, strlen(ts_text))) {
            esc_ts = sheet_filter_escape(tspath);
            strbuf_printf(&filter,
                          ",drawtext=fontfile=%s:textfile=%s:expansion=none"
                          ":x=w-tw-8:y=h-th-8:fontsize=%d:fontcolor=white"
                          ":box=1:boxcolor=black@0.55:boxborderw=5",
                          esc_font, esc_ts, layout_timestamp_font(g->tile_w));
        }
    }

    char seek_s[32], len_s[32], frames_s[16];
    snprintf(seek_s, sizeof(seek_s), "%.3f", seek);
    snprintf(len_s, sizeof(len_s), "%.3f", len);
    snprintf(frames_s, sizeof(frames_s), "%d", frames);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-ss");
    argv_add(&a, seek_s);
    if (len > 0) {
        argv_add(&a, "-t");
        argv_add(&a, len_s);
    }
    argv_add(&a, "-i");
    argv_add(&a, src);
    argv_add(&a, "-an");
    argv_add(&a, "-sn");
    argv_add(&a, "-dn");
    argv_add(&a, "-vf");
    argv_add(&a, filter.data);
    argv_add(&a, "-frames:v");
    argv_add(&a, frames_s);
    if (frames > 1) {
        /* image2 numbers from 1 unless told otherwise, and the collector reads
         * the sequence from zero. */
        argv_add(&a, "-start_number");
        argv_add(&a, "0");
    }
    argv_add(&a, "-y");
    argv_add(&a, out_path);

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

/* Hard link where possible: every step of a loop reuses frames from the same
 * sample, and copying a few hundred tiles for the sake of a filename is waste. */
static int place_frame(const char *from, const char *to) {
    unlink(to);
    if (link(from, to) == 0) return 1;
    return fs_copy_file(from, to);
}

/*
 * Frames are extracted to raw_SSS_KK.png and then linked into one contiguous
 * s<step>_NNN.png sequence per animation step, because ffmpeg's image2 reader
 * stops at the first gap in a %03d sequence — one failed seek would otherwise
 * silently truncate the sheet.
 *
 * Returns the number of samples that made it, which is the tile count every
 * step's sequence holds.
 */
static int extract_all(const sheet_ctx_t *ctx, const char *src, const media_info_t *mi, int n,
                       const grid_t *g, int steps, int fps) {
    int jobs = ctx->jobs < 1 ? 1 : ctx->jobs;
    if (jobs > MAX_JOBS) jobs = MAX_JOBS;
    if (jobs > n) jobs = n;

    int active = 0;

    for (int i = 0; i < n; i++) {
        double at, win;
        sample_window(mi->duration, n, i, &at, &win);

        double seek = at, len = win;
        if (steps > 1) gif_segment(mi->duration, at, steps, fps, &seek, &len);

        /* A literal %02d for the multi-frame case; a fixed _00 for a still, so
         * one collection loop covers both. */
        char name[64];
        snprintf(name, sizeof(name), steps > 1 ? "raw_%03d_%%02d.png" : "raw_%03d_00.png", i);
        char *raw = fs_path_join(ctx->tmpdir, name);
        if (!raw) continue;

        char ts[32];
        fs_hms_short(at, ts, sizeof(ts));

        if (active >= jobs) {
            proc_reap_one();
            active--;
        }

        if (spawn_sample(ctx, src, g, i, seek, len, ctx->no_timestamps ? NULL : ts, raw, steps,
                         fps) > 0) {
            active++;
        }

        free(raw);
    }

    while (active-- > 0) proc_reap_one();

    int got = 0;

    for (int i = 0; i < n; i++) {
        /* Contiguous from zero: a gap mid-run means the segment ran out of
         * source, and the frames past it are not this sample's. */
        int have = 0;
        for (int k = 0; k < steps; k++) {
            char name[64];
            snprintf(name, sizeof(name), "raw_%03d_%02d.png", i, k);
            char *raw = fs_path_join(ctx->tmpdir, name);
            if (!raw) break;

            int present = fs_file_size(raw) > 0;
            free(raw);
            if (!present) break;
            have++;
        }

        if (have == 0) {
            if (!ctx->quiet) {
                double at, win;
                sample_window(mi->duration, n, i, &at, &win);

                char ts[32];
                fs_hms_short(at, ts, sizeof(ts));
                fprintf(stderr, "warning: could not extract frame at %s from %s\n", ts, src);
            }
            continue;
        }

        int placed = 1;
        for (int k = 0; k < steps && placed; k++) {
            /* A segment cut short by the end of the file holds its last frame
             * for the rest of the loop, rather than dropping the tile and
             * reshuffling the whole grid. */
            char from_name[64], to_name[64];
            snprintf(from_name, sizeof(from_name), "raw_%03d_%02d.png", i,
                     k < have ? k : have - 1);
            snprintf(to_name, sizeof(to_name), "s%02d_%03d.png", k, got);

            char *from = fs_path_join(ctx->tmpdir, from_name);
            char *to = fs_path_join(ctx->tmpdir, to_name);

            if (!from || !to || !place_frame(from, to)) placed = 0;

            free(from);
            free(to);
        }

        if (placed) got++;
    }

    return got;
}

/* Tiles one animation step's sequence and stacks the header onto it. */
static int build_step(const sheet_ctx_t *ctx, const grid_t *g, int step, const char *header_png,
                      const char *out_png) {
    char seq_name[64];
    snprintf(seq_name, sizeof(seq_name), "s%02d_%%03d.png", step);

    char *pattern = fs_path_join(ctx->tmpdir, seq_name);
    char *grid_png = fs_path_join(ctx->tmpdir, "grid.png");

    int ok = 0;
    if (!pattern || !grid_png) goto done;

    char tile[128];
    snprintf(tile, sizeof(tile), "tile=%dx%d:padding=%d:margin=%d:color=" SHEET_BG, g->cols,
             g->rows, g->padding, g->margin);

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

    ok = sheet_stack(ctx, header_png, grid_png, out_png);

done:
    free(pattern);
    free(grid_png);
    return ok;
}

/*
 * One palette for the whole loop rather than per frame: a palette that shifts
 * between frames makes static areas — the header, the background — crawl.
 * stats_mode=diff weights the tiles, which are the part that actually moves,
 * and ordered dithering keeps the noise from re-randomising every frame, which
 * both looks calmer and compresses far better than error diffusion.
 */
static int encode_gif(const sheet_ctx_t *ctx, int fps, const char *out_gif) {
    char *pattern = fs_path_join(ctx->tmpdir, "sheet_%02d.png");
    if (!pattern) return 0;

    char fps_s[16];
    snprintf(fps_s, sizeof(fps_s), "%d", fps);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-framerate");
    argv_add(&a, fps_s);
    argv_add(&a, "-start_number");
    argv_add(&a, "0");
    argv_add(&a, "-i");
    argv_add(&a, pattern);
    argv_add(&a, "-filter_complex");
    argv_add(&a, "split[a][b];[a]palettegen=stats_mode=diff[p];"
                 "[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle");
    argv_add(&a, "-loop");
    argv_add(&a, "0");
    argv_add(&a, "-y");
    argv_add(&a, out_gif);

    int ok = sheet_run_ffmpeg(ctx, &a, "encode gif");

    free(pattern);
    return ok;
}

int sheet_video_build(const sheet_ctx_t *ctx, const char *src, const char *out_path) {
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

    int steps = 1;
    int fps = ctx->gif_fps > 0 ? ctx->gif_fps : SHEET_GIF_FPS;

    if (ctx->gif) {
        steps = ctx->gif_frames > 0 ? ctx->gif_frames : SHEET_GIF_FRAMES;
        if (steps > SHEET_GIF_MAX_FRAMES) steps = SHEET_GIF_MAX_FRAMES;
        if (steps < 2) steps = 2;
    }

    int min_tile = ctx->min_tile > 0 ? ctx->min_tile : LAYOUT_MIN_TILE_VIDEO;
    grid_t g = layout_grid(n, mi.width, mi.height, ctx->width, min_tile, ctx->columns);

    if (ctx->dry_run) {
        printf("would write %s  (%d frames, %dx%d grid, %dx%d tiles", out_path, n, g.cols, g.rows,
               g.tile_w, g.tile_h);
        if (ctx->gif) printf(", %d-frame loop at %d fps", steps, fps);
        printf(")\n");
        return 1;
    }

    if (!ctx->quiet) printf("%s\n", out_path);

    int got = extract_all(ctx, src, &mi, n, &g, steps, fps);
    if (got == 0) {
        fprintf(stderr, "error: no frames could be extracted from %s\n", src);
        return 0;
    }

    if (got < n && !ctx->quiet) {
        fprintf(stderr, "warning: extracted %d of %d frames from %s\n", got, n, src);
    }

    layout_set_count(&g, got);

    char *header_png = fs_path_join(ctx->tmpdir, "header.png");
    if (!header_png) return 0;

    char *name = fs_basename_dup(src);
    textbuf_t tb;
    textbuf_init(&tb);
    textbuf_video_header(&tb, name ? name : src, &mi, got);
    free(name);

    /* Rendered once: every step of the loop carries the same header. */
    int ok = sheet_render_header(ctx, &tb, g.grid_w, header_png, NULL);
    textbuf_free(&tb);

    if (steps == 1) {
        if (ok) ok = build_step(ctx, &g, 0, header_png, out_path);
    } else {
        for (int k = 0; ok && k < steps; k++) {
            char step_name[64];
            snprintf(step_name, sizeof(step_name), "sheet_%02d.png", k);

            char *step_png = fs_path_join(ctx->tmpdir, step_name);
            ok = step_png && build_step(ctx, &g, k, header_png, step_png);
            free(step_png);
        }

        if (ok) ok = encode_gif(ctx, fps, out_path);
    }

    free(header_png);
    return ok;
}
