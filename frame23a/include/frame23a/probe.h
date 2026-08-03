#ifndef FRAME23A_PROBE_H
#define FRAME23A_PROBE_H

#include <sys/types.h>

#include "frame23a/tools.h"

typedef struct {
    int has_video;
    int has_audio;

    int width;
    int height;
    double duration; /* seconds; <= 0 when unknown (still images) */
    off_t size;
    long long bit_rate;
    double fps;

    char vcodec[64];
    char vprofile[64];
    char pix_fmt[32];

    char acodec[64];
    int channels;
    int sample_rate;

    char format_name[128];

    int valid;
} media_info_t;

media_info_t probe_file(const tools_t *tools, const char *path);

#endif
