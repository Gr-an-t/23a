#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "frame23a/textbuf.h"

void textbuf_init(textbuf_t *tb) {
    strbuf_init(&tb->sb);
    tb->lines = 0;
}

void textbuf_free(textbuf_t *tb) {
    strbuf_free(&tb->sb);
    tb->lines = 0;
}

void textbuf_line(textbuf_t *tb, const char *fmt, ...) {
    if (tb->lines > 0) strbuf_puts(&tb->sb, "\n");

    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    strbuf_puts(&tb->sb, line);
    tb->lines++;
}

int textbuf_write(const textbuf_t *tb, const char *path) {
    return fs_write_file(path, tb->sb.data ? tb->sb.data : "", tb->sb.len);
}

void textbuf_video_header(textbuf_t *tb, const char *name, const media_info_t *mi, int frames) {
    char size[32], dur[16];
    fs_human_size(mi->size, size, sizeof(size));
    fs_hms(mi->duration, dur, sizeof(dur));

    textbuf_line(tb, "%s", name);

    if (mi->bit_rate > 0) {
        textbuf_line(tb, "Size: %s    Duration: %s    Bitrate: %lld kb/s", size, dur,
                     mi->bit_rate / 1000);
    } else {
        textbuf_line(tb, "Size: %s    Duration: %s", size, dur);
    }

    if (mi->has_video) {
        char profile[72] = "";
        if (mi->vprofile[0]) snprintf(profile, sizeof(profile), " (%s)", mi->vprofile);

        textbuf_line(tb, "Video: %s%s  %dx%d  %.2f fps  %s",
                     mi->vcodec[0] ? mi->vcodec : "unknown", profile,
                     mi->width, mi->height, mi->fps,
                     mi->pix_fmt[0] ? mi->pix_fmt : "?");
    }

    if (mi->has_audio) {
        textbuf_line(tb, "Audio: %s  %d ch  %d Hz",
                     mi->acodec[0] ? mi->acodec : "unknown", mi->channels, mi->sample_rate);
    } else {
        textbuf_line(tb, "Audio: none");
    }

    textbuf_line(tb, "%d frames sampled across %s", frames, dur);
}

void textbuf_image_header(textbuf_t *tb, const char *name, const media_info_t *mi, off_t size) {
    char human[32];
    fs_human_size(size, human, sizeof(human));

    textbuf_line(tb, "%s", name);
    textbuf_line(tb, "Size: %s    Dimensions: %dx%d    Format: %s", human, mi->width, mi->height,
                 mi->vcodec[0] ? mi->vcodec : "unknown");
}

void textbuf_folder_header(textbuf_t *tb, const char *name, int count, off_t total,
                           int page, int pages, int shown, int unreadable) {
    char human[32];
    fs_human_size(total, human, sizeof(human));

    textbuf_line(tb, "%s", name);
    textbuf_line(tb, "%d image%s    Total: %s", count, count == 1 ? "" : "s", human);

    if (pages > 1) textbuf_line(tb, "Page %d of %d", page, pages);

    if (unreadable > 0) {
        textbuf_line(tb, "%d shown, %d could not be read", shown, unreadable);
    }
}
