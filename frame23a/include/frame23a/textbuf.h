#ifndef FRAME23A_TEXTBUF_H
#define FRAME23A_TEXTBUF_H

#include <sys/types.h>

#include "frame23a/fsutil.h"
#include "frame23a/probe.h"

typedef struct {
    strbuf_t sb;
    int lines;
} textbuf_t;

void textbuf_init(textbuf_t *tb);
void textbuf_free(textbuf_t *tb);
void textbuf_line(textbuf_t *tb, const char *fmt, ...);

/*
 * Written verbatim for drawtext's textfile= option, which needs no escaping —
 * inline text= would break on ':', '\'', '\\' and '%' in filenames.
 */
int textbuf_write(const textbuf_t *tb, const char *path);

void textbuf_video_header(textbuf_t *tb, const char *name, const media_info_t *mi, int frames);
void textbuf_image_header(textbuf_t *tb, const char *name, const media_info_t *mi, off_t size);
void textbuf_folder_header(textbuf_t *tb, const char *name, int count, off_t total,
                           int page, int pages);

#endif
