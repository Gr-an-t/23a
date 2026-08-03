#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/probe.h"
#include "frame23a/sheet.h"

#define ASPECT_SAMPLE 16

/*
 * Folders mix portrait and landscape, and any single tile shape letterboxes
 * the rest. Averaging a sample beats trusting whichever file sorted first,
 * without paying an ffprobe spawn for every image in a large folder.
 */
static void sample_aspect(const sheet_ctx_t *ctx, const pathlist_t *images, int *aw, int *ah) {
    int step = images->count / ASPECT_SAMPLE;
    if (step < 1) step = 1;

    double sum = 0.0;
    int seen = 0;

    for (int i = 0; i < images->count; i += step) {
        media_info_t mi = probe_file(ctx->tools, images->items[i]);
        if (mi.width > 0 && mi.height > 0) {
            sum += (double)mi.width / (double)mi.height;
            seen++;
        }
    }

    if (seen == 0) {
        *aw = 4;
        *ah = 3;
        return;
    }

    double avg = sum / seen;
    if (avg < 0.5) avg = 0.5;
    if (avg > 2.0) avg = 2.0;

    *aw = (int)(avg * 1000.0);
    *ah = 1000;
}

static int render_tile(const sheet_ctx_t *ctx, const char *src, const grid_t *g,
                       const char *out_png) {
    /*
     * format=rgb24 is load-bearing, not cosmetic. Images with alpha yield rgba
     * tiles while the rest yield rgb24, and a pixel-format change part-way
     * through the sequence makes ffmpeg reinitialise the filtergraph, which
     * flushes the tile filter early and silently truncates the sheet to
     * however many tiles preceded the change.
     */
    char filter[512];
    snprintf(filter, sizeof(filter),
             "scale=%d:%d:force_original_aspect_ratio=decrease,"
             "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=" SHEET_TILE_BG ",format=rgb24",
             g->tile_w, g->tile_h, g->tile_w, g->tile_h);

    argv_t a;
    argv_reset(&a);
    argv_add(&a, ctx->tools->ffmpeg);
    argv_add(&a, "-hide_banner");
    argv_add(&a, "-v");
    argv_add(&a, "error");
    argv_add(&a, "-nostdin");
    argv_add(&a, "-i");
    argv_add(&a, src);
    argv_add(&a, "-vf");
    argv_add(&a, filter);
    argv_add(&a, "-frames:v");
    argv_add(&a, "1");
    argv_add(&a, "-y");
    argv_add(&a, out_png);

    if (ctx->verbose) {
        char *line = proc_argv_display(&a);
        fprintf(stderr, "  $ %s\n", line);
        free(line);
    }

    proc_result_t res;
    int ok = proc_run_argv(&a, PROC_CAPTURE_ERR, &res);
    proc_result_free(&res);

    if (ok && fs_file_size(out_png) <= 0) ok = 0;
    return ok;
}

static void clear_tiles(const sheet_ctx_t *ctx, int count) {
    for (int i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof(name), "t_%03d.png", i);

        char *p = fs_path_join(ctx->tmpdir, name);
        if (p) unlink(p);
        free(p);
    }
}

static int build_page(const sheet_ctx_t *ctx, const image_group_t *group, grid_t *g,
                      int first, int count, int page, int pages, off_t total_size,
                      const char *out_png, int *out_unreadable) {
    int got = 0;

    for (int i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof(name), "t_%03d.png", got);

        char *dst = fs_path_join(ctx->tmpdir, name);
        if (!dst) continue;

        if (render_tile(ctx, group->images.items[first + i], g, dst)) {
            got++;
        } else if (!ctx->quiet) {
            fprintf(stderr, "warning: skipping unreadable image: %s\n",
                    group->images.items[first + i]);
        }

        free(dst);
    }

    if (out_unreadable) *out_unreadable += count - got;

    if (got == 0) {
        fprintf(stderr, "error: none of the %d image%s for %s could be read\n", count,
                count == 1 ? "" : "s", group->label);
        return 0;
    }

    layout_set_count(g, got);

    char *pattern = fs_path_join(ctx->tmpdir, "t_%03d.png");
    char *grid_png = fs_path_join(ctx->tmpdir, "grid.png");
    char *header_png = fs_path_join(ctx->tmpdir, "header.png");

    int ok = 0;
    if (!pattern || !grid_png || !header_png) goto done;

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

    if (!sheet_run_ffmpeg(ctx, &a, "tile images")) goto done;

    textbuf_t tb;
    textbuf_init(&tb);

    /* A lone image file gets its own detail header; a folder gets a summary. */
    if (group->images.count == 1 && !fs_is_dir(group->source)) {
        media_info_t mi = probe_file(ctx->tools, group->images.items[0]);
        char *base = fs_basename_dup(group->images.items[0]);
        textbuf_image_header(&tb, base ? base : group->label, &mi, total_size);
        free(base);
    } else {
        textbuf_folder_header(&tb, group->label, group->images.count, total_size, page, pages,
                              got, count - got);
    }

    ok = sheet_render_header(ctx, &tb, g->grid_w, header_png, NULL) &&
         sheet_stack(ctx, header_png, grid_png, out_png);

    textbuf_free(&tb);

done:
    clear_tiles(ctx, got);
    free(pattern);
    free(grid_png);
    free(header_png);
    return ok;
}

int sheet_image_build(const sheet_ctx_t *ctx, const image_group_t *group, const char *out_dir,
                      const char *label, int *sheets_written) {
    if (group->images.count == 0) return 0;
    if (!label) label = group->label;

    off_t total_size = 0;
    for (int i = 0; i < group->images.count; i++) {
        off_t s = fs_file_size(group->images.items[i]);
        if (s > 0) total_size += s;
    }

    int aw, ah;
    sample_aspect(ctx, &group->images, &aw, &ah);

    int min_tile = ctx->min_tile > 0 ? ctx->min_tile : LAYOUT_MIN_TILE_IMAGE;
    grid_t probe_grid = layout_grid(group->images.count, aw, ah, ctx->width, min_tile,
                                    ctx->columns);

    header_t est = layout_header(3, probe_grid.grid_w);
    int per_page = ctx->per_page > 0 ? ctx->per_page : layout_per_page(&probe_grid, est.height);
    if (per_page < 1) per_page = 1;

    int pages = (group->images.count + per_page - 1) / per_page;

    /* Columns are fixed from a full page so every page of a set lines up. */
    int shape_for = pages > 1 ? per_page : group->images.count;
    grid_t g = layout_grid(shape_for, aw, ah, ctx->width, min_tile, ctx->columns);

    if (ctx->dry_run) {
        for (int p = 0; p < pages; p++) {
            int count = group->images.count - p * per_page;
            if (count > per_page) count = per_page;

            printf("would write %s/%s%s.png  (%d images, %d cols)\n", out_dir, label,
                   pages > 1 ? "_NN" : "", count, g.cols);
        }
        if (sheets_written) *sheets_written += pages;
        return 1;
    }

    int all_ok = 1;
    int unreadable = 0;

    for (int p = 0; p < pages; p++) {
        int first = p * per_page;
        int count = group->images.count - first;
        if (count > per_page) count = per_page;

        char name[512];
        if (pages > 1) {
            snprintf(name, sizeof(name), "%s_%02d.png", label, p + 1);
        } else {
            snprintf(name, sizeof(name), "%s.png", label);
        }

        char *out_png = fs_path_join(out_dir, name);
        if (!out_png) {
            all_ok = 0;
            continue;
        }

        if (!ctx->quiet) printf("%s\n", out_png);

        if (build_page(ctx, group, &g, first, count, p + 1, pages, total_size, out_png,
                       &unreadable)) {
            if (sheets_written) (*sheets_written)++;
        } else {
            all_ok = 0;
        }

        free(out_png);
    }

    /*
     * Reported even under -q: a sheet that quietly omits half the folder is
     * worse than a noisy one. Common cause is HEIC without libheif support.
     */
    if (unreadable > 0) {
        fprintf(stderr, "warning: %d of %d image%s in %s could not be read and %s not on the "
                        "sheet\n",
                unreadable, group->images.count, group->images.count == 1 ? "" : "s",
                group->label, unreadable == 1 ? "is" : "are");
    }

    return all_ok;
}
