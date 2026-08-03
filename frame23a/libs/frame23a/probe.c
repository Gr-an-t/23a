#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame23a/fsutil.h"
#include "frame23a/probe.h"
#include "frame23a/proc.h"

/* Buffered key/values for one [STREAM] block; codec_type arrives mid-block,
 * so a stream cannot be classified until [/STREAM] is reached. */
typedef struct {
    char codec_type[16];
    char codec_name[64];
    char profile[64];
    char pix_fmt[32];
    int width;
    int height;
    int channels;
    int sample_rate;
    double fps;
} stream_block_t;

static int is_na(const char *v) {
    return !v || !*v || strcmp(v, "N/A") == 0 || strcmp(v, "unknown") == 0;
}

static void copy_field(char *dst, size_t cap, const char *src) {
    if (is_na(src)) return;
    snprintf(dst, cap, "%s", src);
}

static double parse_rational(const char *v) {
    if (is_na(v)) return 0.0;

    double num = 0.0, den = 0.0;
    if (sscanf(v, "%lf/%lf", &num, &den) == 2) {
        return den > 0.0 ? num / den : 0.0;
    }
    return atof(v);
}

static void commit_stream(media_info_t *info, const stream_block_t *s) {
    if (strcmp(s->codec_type, "video") == 0) {
        /* Keep the first video stream; later ones are usually cover art. */
        if (info->has_video) return;

        info->has_video = 1;
        info->width = s->width;
        info->height = s->height;
        info->fps = s->fps;
        memcpy(info->vcodec, s->codec_name, sizeof(info->vcodec));
        memcpy(info->vprofile, s->profile, sizeof(info->vprofile));
        memcpy(info->pix_fmt, s->pix_fmt, sizeof(info->pix_fmt));
    } else if (strcmp(s->codec_type, "audio") == 0) {
        if (info->has_audio) return;

        info->has_audio = 1;
        info->channels = s->channels;
        info->sample_rate = s->sample_rate;
        memcpy(info->acodec, s->codec_name, sizeof(info->acodec));
    }
}

media_info_t probe_file(const tools_t *tools, const char *path) {
    media_info_t info = {0};
    info.size = fs_file_size(path);

    const char *argv[] = {
        tools->ffprobe,
        "-v", "error",
        "-show_entries",
        "format=duration,size,format_name,bit_rate:"
        "stream=codec_type,codec_name,profile,width,height,"
        "r_frame_rate,pix_fmt,channels,sample_rate",
        "-of", "default",
        path,
        NULL,
    };

    proc_result_t res;
    if (!proc_run(argv, PROC_CAPTURE_OUT | PROC_CAPTURE_ERR, &res) || !res.out) {
        proc_result_free(&res);
        return info;
    }

    stream_block_t cur = {0};
    int in_stream = 0;

    char *save = NULL;
    for (char *line = strtok_r(res.out, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {

        if (strcmp(line, "[STREAM]") == 0) {
            memset(&cur, 0, sizeof(cur));
            in_stream = 1;
            continue;
        }
        if (strcmp(line, "[/STREAM]") == 0) {
            commit_stream(&info, &cur);
            in_stream = 0;
            continue;
        }
        if (line[0] == '[') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        const char *key = line;
        const char *val = eq + 1;

        if (in_stream) {
            if (strcmp(key, "codec_type") == 0) copy_field(cur.codec_type, sizeof(cur.codec_type), val);
            else if (strcmp(key, "codec_name") == 0) copy_field(cur.codec_name, sizeof(cur.codec_name), val);
            else if (strcmp(key, "profile") == 0) copy_field(cur.profile, sizeof(cur.profile), val);
            else if (strcmp(key, "pix_fmt") == 0) copy_field(cur.pix_fmt, sizeof(cur.pix_fmt), val);
            else if (strcmp(key, "width") == 0) cur.width = atoi(val);
            else if (strcmp(key, "height") == 0) cur.height = atoi(val);
            else if (strcmp(key, "channels") == 0) cur.channels = atoi(val);
            else if (strcmp(key, "sample_rate") == 0) cur.sample_rate = is_na(val) ? 0 : atoi(val);
            else if (strcmp(key, "r_frame_rate") == 0) cur.fps = parse_rational(val);
        } else {
            if (strcmp(key, "duration") == 0) info.duration = is_na(val) ? 0.0 : atof(val);
            else if (strcmp(key, "bit_rate") == 0) info.bit_rate = is_na(val) ? 0 : atoll(val);
            else if (strcmp(key, "format_name") == 0) copy_field(info.format_name, sizeof(info.format_name), val);
            else if (strcmp(key, "size") == 0 && info.size < 0) info.size = (off_t)atoll(val);
        }
    }

    info.valid = info.has_video || info.has_audio;
    proc_result_free(&res);
    return info;
}
