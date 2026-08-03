#ifndef FRAME23A_TOOLS_H
#define FRAME23A_TOOLS_H

typedef struct {
    char *ffmpeg;
    char *ffprobe;
    char *exiftool; /* optional; NULL when not installed */
    char *font;
    int valid;
} tools_t;

tools_t tools_discover(const char *font_override);
void tools_free(tools_t *t);

/* Font lookup on its own, so check-deps can report each dependency
 * independently instead of stopping at the first missing one. */
char *tools_find_font(const char *font_override);

/* "sudo pacman -S ffmpeg" vs "sudo apt install ffmpeg", per /etc/os-release. */
const char *tools_install_hint(const char *pkg_generic);

#endif
