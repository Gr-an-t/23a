#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame23a/fsutil.h"
#include "frame23a/scan.h"

static const char *const video_exts[] = {
    "mp4", "mkv", "mov", "avi", "webm", "m4v", "mpg", "mpeg", "wmv",
    "flv", "ts", "m2ts", "mts", "vob", "3gp", "ogv", "asf", "divx", NULL,
};

static const char *const image_exts[] = {
    "jpg", "jpeg", "png", "gif", "bmp", "tif", "tiff", "webp",
    "heic", "heif", "avif", "jfif", "ppm", "pgm", NULL,
};

void pathlist_init(pathlist_t *l) {
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void pathlist_free(pathlist_t *l) {
    for (int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    pathlist_init(l);
}

int pathlist_push(pathlist_t *l, const char *path) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        char **p = realloc(l->items, (size_t)cap * sizeof(char *));
        if (!p) return 0;
        l->items = p;
        l->cap = cap;
    }

    char *dup = fs_strdup(path);
    if (!dup) return 0;

    l->items[l->count++] = dup;
    return 1;
}

static int cmp_path(const void *a, const void *b) {
    const char *const *pa = a;
    const char *const *pb = b;
    return strcmp(*pa, *pb);
}

/* readdir order is arbitrary; sorting keeps sheets reproducible run to run. */
void pathlist_sort(pathlist_t *l) {
    if (l->count > 1) qsort(l->items, (size_t)l->count, sizeof(char *), cmp_path);
}

void worklist_init(worklist_t *w) {
    pathlist_init(&w->videos);
    w->groups = NULL;
    w->group_count = 0;
    w->group_cap = 0;
}

void worklist_free(worklist_t *w) {
    pathlist_free(&w->videos);

    for (int i = 0; i < w->group_count; i++) {
        free(w->groups[i].label);
        free(w->groups[i].source);
        pathlist_free(&w->groups[i].images);
    }
    free(w->groups);

    w->groups = NULL;
    w->group_count = 0;
    w->group_cap = 0;
}

static image_group_t *worklist_add_group(worklist_t *w, const char *label, const char *source) {
    if (w->group_count == w->group_cap) {
        int cap = w->group_cap ? w->group_cap * 2 : 8;
        image_group_t *p = realloc(w->groups, (size_t)cap * sizeof(image_group_t));
        if (!p) return NULL;
        w->groups = p;
        w->group_cap = cap;
    }

    image_group_t *g = &w->groups[w->group_count];
    g->label = fs_strdup(label);
    g->source = fs_strdup(source);
    pathlist_init(&g->images);

    if (!g->label || !g->source) {
        free(g->label);
        free(g->source);
        return NULL;
    }

    w->group_count++;
    return g;
}

media_kind_t scan_classify(const char *path) {
    if (fs_ext_is(path, video_exts)) return MEDIA_VIDEO;
    if (fs_ext_is(path, image_exts)) return MEDIA_IMAGE;
    return MEDIA_OTHER;
}

/*
 * Names the sheet after the folder's real name. Taking the basename of the
 * path as typed would yield "." for a bare `frame23a` in the current
 * directory, producing a file called "..png".
 */
static char *dir_label(const char *dir) {
    char *abs = realpath(dir, NULL);
    char *label = fs_basename_dup(abs ? abs : dir);
    free(abs);

    if (label && (strcmp(label, "/") == 0 || strcmp(label, ".") == 0)) {
        free(label);
        label = fs_strdup("root");
    }

    return label;
}

static int collect_dir(worklist_t *w, const char *dir, int recursive) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "warning: cannot open directory: %s\n", dir);
        return 0;
    }

    pathlist_t images;
    pathlist_t subdirs;
    pathlist_init(&images);
    pathlist_init(&subdirs);

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char *child = fs_path_join(dir, e->d_name);
        if (!child) continue;

        if (fs_is_dir(child)) {
            if (recursive) pathlist_push(&subdirs, child);
        } else if (fs_is_file(child)) {
            switch (scan_classify(child)) {
                case MEDIA_VIDEO: pathlist_push(&w->videos, child); break;
                case MEDIA_IMAGE: pathlist_push(&images, child); break;
                case MEDIA_OTHER: break;
            }
        }

        free(child);
    }
    closedir(d);

    /* One sheet per folder, so the folder's images become a single group. */
    if (images.count > 0) {
        char *label = dir_label(dir);
        image_group_t *g = label ? worklist_add_group(w, label, dir) : NULL;
        free(label);

        if (g) {
            pathlist_sort(&images);
            for (int i = 0; i < images.count; i++) pathlist_push(&g->images, images.items[i]);
        }
    }
    pathlist_free(&images);

    pathlist_sort(&subdirs);
    for (int i = 0; i < subdirs.count; i++) collect_dir(w, subdirs.items[i], recursive);
    pathlist_free(&subdirs);

    return 1;
}

static int count_media_recursive(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    int n = 0;
    struct dirent *e;

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char *child = fs_path_join(dir, e->d_name);
        if (!child) continue;

        if (fs_is_dir(child)) {
            n += count_media_recursive(child);
        } else if (fs_is_file(child) && scan_classify(child) != MEDIA_OTHER) {
            n++;
        }

        free(child);
    }

    closedir(d);
    return n;
}

int scan_count_nested(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    int n = 0;
    struct dirent *e;

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char *child = fs_path_join(dir, e->d_name);
        if (!child) continue;

        if (fs_is_dir(child)) n += count_media_recursive(child);
        free(child);
    }

    closedir(d);
    return n;
}

int scan_collect(worklist_t *w, const char *path, int recursive) {
    if (fs_is_dir(path)) return collect_dir(w, path, recursive);

    if (!fs_is_file(path)) {
        fprintf(stderr, "warning: not a file or directory: %s\n", path);
        return 0;
    }

    switch (scan_classify(path)) {
        case MEDIA_VIDEO:
            return pathlist_push(&w->videos, path);

        case MEDIA_IMAGE: {
            char *stem = fs_stem_dup(path);
            if (!stem) return 0;

            image_group_t *g = worklist_add_group(w, stem, path);
            free(stem);

            return g ? pathlist_push(&g->images, path) : 0;
        }

        case MEDIA_OTHER:
            fprintf(stderr, "warning: unrecognised media type, skipping: %s\n", path);
            return 0;
    }

    return 0;
}
