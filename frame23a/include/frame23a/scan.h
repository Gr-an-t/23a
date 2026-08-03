#ifndef FRAME23A_SCAN_H
#define FRAME23A_SCAN_H

typedef enum { MEDIA_VIDEO, MEDIA_IMAGE, MEDIA_OTHER } media_kind_t;

typedef struct {
    char **items;
    int count;
    int cap;
} pathlist_t;

void pathlist_init(pathlist_t *l);
void pathlist_free(pathlist_t *l);
int pathlist_push(pathlist_t *l, const char *path);
void pathlist_sort(pathlist_t *l);

/* Images sharing one output sheet: a whole folder, or a lone image file. */
typedef struct {
    char *label;
    char *source;
    pathlist_t images;
} image_group_t;

typedef struct {
    pathlist_t videos;
    image_group_t *groups;
    int group_count;
    int group_cap;
} worklist_t;

void worklist_init(worklist_t *w);
void worklist_free(worklist_t *w);

media_kind_t scan_classify(const char *path);
int scan_collect(worklist_t *w, const char *path, int recursive);

/* Media sitting in subfolders that a flat scan would pass over, so the
 * omission can be reported rather than left silent. */
int scan_count_nested(const char *dir);

#endif
