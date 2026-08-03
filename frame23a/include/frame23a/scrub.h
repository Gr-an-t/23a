#ifndef FRAME23A_SCRUB_H
#define FRAME23A_SCRUB_H

typedef enum {
    SCRUB_OK,
    SCRUB_UNSUPPORTED,
    SCRUB_ERROR,
} scrub_result_t;

/*
 * Rewrites src to dst with identifying metadata removed and the compressed
 * pixel data copied byte for byte, so the result is lossless.
 */
scrub_result_t scrub_image(const char *src, const char *dst);

#endif
