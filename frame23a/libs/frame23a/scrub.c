#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame23a/fsutil.h"
#include "frame23a/scrub.h"

/* ---------------------------------------------------------------- JPEG --- */

#define M_SOI 0xD8
#define M_EOI 0xD9
#define M_SOS 0xDA
#define M_APP0 0xE0
#define M_APP1 0xE1
#define M_APP2 0xE2
#define M_APP15 0xEF
#define M_COM 0xFE

static unsigned rd16(const unsigned char *p, int big_endian) {
    return big_endian ? (unsigned)(p[0] << 8 | p[1]) : (unsigned)(p[1] << 8 | p[0]);
}

static unsigned rd32(const unsigned char *p, int big_endian) {
    if (big_endian) return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
    return (unsigned)p[3] << 24 | (unsigned)p[2] << 16 | (unsigned)p[1] << 8 | p[0];
}

/*
 * Pulls just the Orientation tag out of an EXIF APP1 payload. Dropping EXIF
 * wholesale would silently rotate every photo shot in portrait, so this one
 * value is carried across; everything else in the block is discarded.
 */
static int exif_orientation(const unsigned char *seg, size_t len) {
    if (len < 14 || memcmp(seg, "Exif\0\0", 6) != 0) return 0;

    const unsigned char *tiff = seg + 6;
    size_t tiff_len = len - 6;

    int big;
    if (memcmp(tiff, "MM", 2) == 0) {
        big = 1;
    } else if (memcmp(tiff, "II", 2) == 0) {
        big = 0;
    } else {
        return 0;
    }

    if (rd16(tiff + 2, big) != 42) return 0;

    unsigned ifd = rd32(tiff + 4, big);
    if (ifd + 2 > tiff_len) return 0;

    unsigned count = rd16(tiff + ifd, big);
    for (unsigned i = 0; i < count; i++) {
        size_t off = ifd + 2 + (size_t)i * 12;
        if (off + 12 > tiff_len) break;

        const unsigned char *e = tiff + off;
        if (rd16(e, big) != 0x0112) continue;
        if (rd16(e + 2, big) != 3) continue;

        unsigned v = rd16(e + 8, big);
        return (v >= 1 && v <= 8) ? (int)v : 0;
    }

    return 0;
}

/* A 34-byte APP1 holding one IFD0 entry: Orientation and nothing else. */
static void write_orientation_app1(FILE *out, int orientation) {
    unsigned char seg[36] = {
        0xFF, M_APP1, 0x00, 0x22,
        'E', 'x', 'i', 'f', 0x00, 0x00,
        'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x01,
        0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
        0x00, (unsigned char)orientation, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    fwrite(seg, 1, sizeof(seg), out);
}

static int jpeg_keep_marker(unsigned char m, const unsigned char *payload, size_t plen) {
    if (m == M_COM) return 0;

    /* APP0/JFIF carries only density and thumbnail geometry. */
    if (m == M_APP0) return 1;

    /* APP2 is ICC colour management, not identity; keep it so colours hold. */
    if (m == M_APP2) return plen >= 11 && memcmp(payload, "ICC_PROFILE", 11) == 0;

    /* APP1 (EXIF and XMP), APP13 (IPTC) and every other APPn go. */
    if (m >= M_APP1 && m <= M_APP15) return 0;

    return 1;
}

static scrub_result_t scrub_jpeg(const unsigned char *data, size_t len, const char *dst) {
    if (len < 4 || data[0] != 0xFF || data[1] != M_SOI) return SCRUB_UNSUPPORTED;

    FILE *out = fopen(dst, "wb");
    if (!out) return SCRUB_ERROR;

    fputc(0xFF, out);
    fputc(M_SOI, out);

    size_t pos = 2;
    int orientation = 0;
    int wrote_orientation = 0;
    scrub_result_t rc = SCRUB_OK;

    while (pos + 1 < len) {
        if (data[pos] != 0xFF) {
            rc = SCRUB_UNSUPPORTED;
            break;
        }

        /* Fill bytes: any run of 0xFF before the marker id. */
        size_t m_at = pos + 1;
        while (m_at < len && data[m_at] == 0xFF) m_at++;
        if (m_at >= len) break;

        unsigned char m = data[m_at];

        if (m == M_EOI) {
            fputc(0xFF, out);
            fputc(M_EOI, out);
            pos = m_at + 1;
            break;
        }

        if (m_at + 2 >= len) {
            rc = SCRUB_UNSUPPORTED;
            break;
        }

        size_t seglen = rd16(data + m_at + 1, 1);
        if (seglen < 2 || m_at + 1 + seglen > len) {
            rc = SCRUB_UNSUPPORTED;
            break;
        }

        const unsigned char *payload = data + m_at + 3;
        size_t plen = seglen - 2;

        if (m == M_APP1 && !orientation) orientation = exif_orientation(payload, plen);

        if (jpeg_keep_marker(m, payload, plen)) {
            /* Orientation must precede the frame data it applies to. */
            if (orientation > 1 && !wrote_orientation) {
                write_orientation_app1(out, orientation);
                wrote_orientation = 1;
            }

            fputc(0xFF, out);
            fputc(m, out);
            fwrite(data + m_at + 1, 1, seglen, out);
        }

        pos = m_at + 1 + seglen;

        if (m == M_SOS) {
            /* Entropy-coded scan runs to the end; copying it verbatim keeps
             * the image bit-for-bit identical. */
            fwrite(data + pos, 1, len - pos, out);
            pos = len;
            break;
        }
    }

    if (fclose(out) != 0) rc = SCRUB_ERROR;
    if (rc != SCRUB_OK) remove(dst);
    return rc;
}

/* ----------------------------------------------------------------- PNG --- */

static const char *const png_keep[] = {
    "IHDR", "PLTE", "IDAT", "IEND", "tRNS", "gAMA", "cHRM", "sRGB",
    "iCCP", "sBIT", "pHYs", "bKGD", "hIST", "sPLT",
    "acTL", "fcTL", "fdAT", /* APNG: dropping these kills the animation */
    NULL,
};

static int png_keep_chunk(const char *type) {
    for (int i = 0; png_keep[i]; i++) {
        if (memcmp(type, png_keep[i], 4) == 0) return 1;
    }

    /* Critical chunks (uppercase first letter) are required to decode, so an
     * unknown one is kept; unknown ancillary chunks are dropped as metadata. */
    return type[0] >= 'A' && type[0] <= 'Z';
}

static scrub_result_t scrub_png(const unsigned char *data, size_t len, const char *dst) {
    static const unsigned char sig[8] = {137, 'P', 'N', 'G', '\r', '\n', 26, '\n'};

    if (len < 8 || memcmp(data, sig, 8) != 0) return SCRUB_UNSUPPORTED;

    FILE *out = fopen(dst, "wb");
    if (!out) return SCRUB_ERROR;

    fwrite(sig, 1, 8, out);

    size_t pos = 8;
    scrub_result_t rc = SCRUB_OK;

    while (pos + 8 <= len) {
        unsigned dlen = rd32(data + pos, 1);
        const char *type = (const char *)(data + pos + 4);

        size_t total = 12 + (size_t)dlen;
        if (dlen > len || pos + total > len) {
            rc = SCRUB_UNSUPPORTED;
            break;
        }

        /* Copied whole, CRC included, so no checksum needs recomputing. */
        if (png_keep_chunk(type)) fwrite(data + pos, 1, total, out);

        pos += total;

        if (memcmp(type, "IEND", 4) == 0) break;
    }

    if (fclose(out) != 0) rc = SCRUB_ERROR;
    if (rc != SCRUB_OK) remove(dst);
    return rc;
}

/* ------------------------------------------------------------ dispatch --- */

scrub_result_t scrub_image(const char *src, const char *dst) {
    size_t len = 0;
    unsigned char *data = fs_read_file(src, &len);
    if (!data) return SCRUB_ERROR;

    scrub_result_t rc = SCRUB_UNSUPPORTED;

    if (len >= 2 && data[0] == 0xFF && data[1] == M_SOI) {
        rc = scrub_jpeg(data, len, dst);
    } else if (len >= 8 && data[0] == 137 && memcmp(data + 1, "PNG", 3) == 0) {
        rc = scrub_png(data, len, dst);
    }

    free(data);
    return rc;
}
