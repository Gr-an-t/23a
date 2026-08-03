#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/run.h"
#include "frame23a/scan.h"
#include "frame23a/sheet.h"
#include "frame23a/strip_meta.h"

static char *g_tmpdir = NULL;

static void cleanup_tmpdir(void) {
    if (!g_tmpdir) return;
    fs_rmtree(g_tmpdir);
    free(g_tmpdir);
    g_tmpdir = NULL;
}

static void on_signal(int sig) {
    cleanup_tmpdir();
    _exit(128 + sig);
}

static char *make_tmpdir(void) {
    g_tmpdir = fs_tempdir_create();
    if (!g_tmpdir) {
        fprintf(stderr, "error: cannot create temporary directory\n");
        return NULL;
    }

    atexit(cleanup_tmpdir);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    return g_tmpdir;
}

char *run_default_output(const char *input, const char *leaf) {
    /* realpath first: "one level up" from a bare relative name like clip.mp4
     * is meaningless until the path is absolute. */
    char *abs = realpath(input, NULL);
    if (!abs) {
        fprintf(stderr, "error: cannot resolve path: %s\n", input);
        return NULL;
    }

    char *base = fs_is_dir(abs) ? fs_strdup(abs) : fs_dirname_dup(abs);
    free(abs);
    if (!base) return NULL;

    char *parent = fs_dirname_dup(base);
    free(base);
    if (!parent) return NULL;

    char *out = fs_path_join(parent, leaf);
    free(parent);
    return out;
}

/*
 * Two videos with the same stem in different subfolders would otherwise
 * silently overwrite one another under -R. Names already claimed in this run
 * get a suffix; names from previous runs are overwritten as normal.
 */
static char *claim_stem(pathlist_t *used, const char *stem) {
    char candidate[512];
    snprintf(candidate, sizeof(candidate), "%s", stem);

    for (int n = 2; n < 1000; n++) {
        int taken = 0;
        for (int i = 0; i < used->count; i++) {
            if (strcmp(used->items[i], candidate) == 0) {
                taken = 1;
                break;
            }
        }
        if (!taken) break;

        snprintf(candidate, sizeof(candidate), "%s_%d", stem, n);
    }

    pathlist_push(used, candidate);
    return fs_strdup(candidate);
}

static int ensure_dir(const char *path, int dry_run) {
    if (dry_run) return 1;

    if (!fs_mkdir_p(path)) {
        fprintf(stderr, "error: cannot create directory: %s\n", path);
        return 0;
    }
    return 1;
}

int run_sheet(const cli_args_t *args, const tools_t *tools) {
    const char *tmpdir = make_tmpdir();
    if (!tmpdir) return 1;

    sheet_ctx_t ctx = {
        .tools = tools,
        .tmpdir = tmpdir,
        .width = args->width,
        .columns = args->columns,
        .count = args->count,
        .min_tile = args->min_tile,
        .per_page = args->per_page,
        .no_timestamps = args->no_timestamps,
        .jobs = args->jobs,
        .dry_run = args->dry_run,
        .verbose = args->verbose,
        .quiet = args->quiet,
    };

    int written = 0;
    int failed = 0;

    for (int p = 0; p < args->path_count; p++) {
        const char *input = args->paths[p];

        char *out_root = args->output ? fs_strdup(args->output)
                                      : run_default_output(input, "contact_sheets");
        if (!out_root) {
            failed++;
            continue;
        }

        worklist_t w;
        worklist_init(&w);
        scan_collect(&w, input, args->recursive);

        if (w.videos.count == 0 && w.group_count == 0) {
            fprintf(stderr, "warning: no media found in %s\n", input);
            worklist_free(&w);
            free(out_root);
            continue;
        }

        pathlist_sort(&w.videos);

        char *video_dir = fs_path_join(out_root, "videos");
        char *image_dir = fs_path_join(out_root, "images");

        /* Separate name pools: videos/ and images/ are distinct directories,
         * so a video and an image sheet may share a stem without conflict. */
        pathlist_t used_videos;
        pathlist_t used_images;
        pathlist_init(&used_videos);
        pathlist_init(&used_images);

        if (w.videos.count > 0 && video_dir && ensure_dir(video_dir, args->dry_run)) {
            for (int i = 0; i < w.videos.count; i++) {
                char *raw = fs_stem_dup(w.videos.items[i]);
                char *stem = raw ? claim_stem(&used_videos, raw) : NULL;
                free(raw);

                char name[512];
                snprintf(name, sizeof(name), "%s.png", stem ? stem : "sheet");
                char *out_png = stem ? fs_path_join(video_dir, name) : NULL;
                free(stem);

                if (!out_png) {
                    failed++;
                    continue;
                }

                if (sheet_video_build(&ctx, w.videos.items[i], out_png)) {
                    written++;
                } else {
                    failed++;
                }
                free(out_png);
            }
        }

        if (w.group_count > 0 && image_dir && ensure_dir(image_dir, args->dry_run)) {
            for (int i = 0; i < w.group_count; i++) {
                char *label = claim_stem(&used_images, w.groups[i].label);

                if (!sheet_image_build(&ctx, &w.groups[i], image_dir, label, &written)) failed++;
                free(label);
            }
        }

        pathlist_free(&used_videos);
        pathlist_free(&used_images);
        free(video_dir);
        free(image_dir);
        worklist_free(&w);
        free(out_root);
    }

    if (!args->quiet) {
        fprintf(stderr, "%s %d sheet%s%s\n", args->dry_run ? "would write" : "wrote", written,
                written == 1 ? "" : "s", failed ? "" : "");
        if (failed) fprintf(stderr, "%d input%s failed\n", failed, failed == 1 ? "" : "s");
    }

    return failed ? 1 : 0;
}

int run_remove_metadata(const cli_args_t *args, const tools_t *tools) {
    const char *tmpdir = make_tmpdir();
    if (!tmpdir) return 1;

    strip_ctx_t ctx = {
        .tools = tools,
        .tmpdir = tmpdir,
        .in_place = args->in_place,
        .dry_run = args->dry_run,
        .verbose = args->verbose,
        .quiet = args->quiet,
    };

    int cleaned = 0;
    int skipped = 0;
    int failed = 0;

    for (int p = 0; p < args->path_count; p++) {
        const char *input = args->paths[p];

        char *out_root = NULL;
        if (!args->in_place) {
            out_root = args->output ? fs_strdup(args->output)
                                    : run_default_output(input, "cleaned");
            if (!out_root) {
                failed++;
                continue;
            }
            if (!ensure_dir(out_root, args->dry_run)) {
                free(out_root);
                failed++;
                continue;
            }
        }

        worklist_t w;
        worklist_init(&w);
        scan_collect(&w, input, args->recursive);
        pathlist_sort(&w.videos);

        for (int i = 0; i < w.videos.count; i++) {
            switch (strip_file(&ctx, w.videos.items[i], out_root)) {
                case STRIP_OK: cleaned++; break;
                case STRIP_SKIPPED: skipped++; break;
                case STRIP_FAILED: failed++; break;
            }
        }

        for (int gi = 0; gi < w.group_count; gi++) {
            const image_group_t *g = &w.groups[gi];
            for (int i = 0; i < g->images.count; i++) {
                switch (strip_file(&ctx, g->images.items[i], out_root)) {
                    case STRIP_OK: cleaned++; break;
                    case STRIP_SKIPPED: skipped++; break;
                    case STRIP_FAILED: failed++; break;
                }
            }
        }

        worklist_free(&w);
        free(out_root);
    }

    if (!args->quiet) {
        fprintf(stderr, "%s %d file%s\n", args->dry_run ? "would clean" : "cleaned", cleaned,
                cleaned == 1 ? "" : "s");
        if (skipped) fprintf(stderr, "%d skipped\n", skipped);
        if (failed) fprintf(stderr, "%d failed\n", failed);
    }

    return failed ? 1 : 0;
}

int run_check_deps(void) {
    /* Each dependency is probed on its own so one missing tool does not mask
     * the status of the others. */
    char *ffmpeg = proc_which("ffmpeg");
    char *ffprobe = proc_which("ffprobe");
    char *exiftool = proc_which("exiftool");
    char *font = tools_find_font(NULL);

    printf("ffmpeg    %s\n", ffmpeg ? ffmpeg : "MISSING");
    printf("ffprobe   %s\n", ffprobe ? ffprobe : "MISSING");
    printf("font      %s\n", font ? font : "MISSING");
    printf("exiftool  %s\n", exiftool ? exiftool : "not installed (optional)");

    int ok = ffmpeg && ffprobe && font;

    if (!ok) {
        printf("\nmissing required dependencies:\n");
        if (!ffmpeg || !ffprobe) printf("  ffmpeg (provides ffprobe): %s\n",
                                        tools_install_hint("ffmpeg"));
        if (!font) printf("  a TrueType font: %s\n", tools_install_hint("font"));
    }

    if (!exiftool) {
        printf("\nexiftool is only needed to scrub HEIC/TIFF/WebP metadata;\n");
        printf("JPEG, PNG and video are handled natively. To add it:\n");
        printf("  %s\n", tools_install_hint("exiftool"));
    }

    if (ok) printf("\nall required dependencies present\n");

    free(ffmpeg);
    free(ffprobe);
    free(exiftool);
    free(font);
    return ok ? 0 : 1;
}
