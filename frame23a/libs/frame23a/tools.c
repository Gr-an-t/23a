#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/proc.h"
#include "frame23a/tools.h"

/*
 * Package names differ per distro. ffmpeg happens to be spelled the same
 * everywhere; exiftool does not, which is the reason this table exists.
 */
typedef enum { DISTRO_ARCH, DISTRO_DEBIAN, DISTRO_UNKNOWN } distro_t;

static distro_t detect_distro(void) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return DISTRO_UNKNOWN;

    char line[256];
    distro_t d = DISTRO_UNKNOWN;

    while (fgets(line, sizeof(line), f)) {
        const char *val = NULL;
        if (strncmp(line, "ID=", 3) == 0) {
            val = line + 3;
        } else if (strncmp(line, "ID_LIKE=", 8) == 0) {
            val = line + 8;
        }
        if (!val) continue;

        if (strstr(val, "arch")) {
            d = DISTRO_ARCH;
            break;
        }
        if (strstr(val, "debian") || strstr(val, "ubuntu")) {
            d = DISTRO_DEBIAN;
            break;
        }
    }

    fclose(f);
    return d;
}

const char *tools_install_hint(const char *pkg_generic) {
    static char buf[160];
    distro_t d = detect_distro();

    const char *arch = pkg_generic;
    const char *debian = pkg_generic;

    if (strcmp(pkg_generic, "exiftool") == 0) {
        arch = "perl-image-exiftool";
        debian = "libimage-exiftool-perl";
    } else if (strcmp(pkg_generic, "font") == 0) {
        arch = "ttf-dejavu";
        debian = "fonts-dejavu-core";
    }

    switch (d) {
        case DISTRO_ARCH:
            snprintf(buf, sizeof(buf), "sudo pacman -S %s", arch);
            break;
        case DISTRO_DEBIAN:
            snprintf(buf, sizeof(buf), "sudo apt install %s", debian);
            break;
        default:
            snprintf(buf, sizeof(buf), "install %s (or %s) with your package manager", arch, debian);
            break;
    }

    return buf;
}

static char *font_from_fc_match(void) {
    char *fc = proc_which("fc-match");
    if (!fc) return NULL;

    const char *argv[] = {fc, "-f", "%{file}", "sans-serif", NULL};
    proc_result_t res;
    char *font = NULL;

    if (proc_run(argv, PROC_CAPTURE_OUT, &res) && res.out && *res.out) {
        proc_chomp(res.out);
        if (fs_is_file(res.out)) font = fs_strdup(res.out);
    }

    proc_result_free(&res);
    free(fc);
    return font;
}

/* Arch and Debian disagree on font layout, so both trees are probed. */
static char *font_from_candidates(void) {
    static const char *const paths[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        NULL,
    };

    for (int i = 0; paths[i]; i++) {
        if (fs_is_file(paths[i])) return fs_strdup(paths[i]);
    }
    return NULL;
}

char *tools_find_font(const char *font_override) {
    if (font_override && *font_override) {
        return fs_is_file(font_override) ? fs_strdup(font_override) : NULL;
    }

    char *font = font_from_fc_match();
    if (!font) font = font_from_candidates();
    return font;
}

tools_t tools_discover(const char *font_override) {
    tools_t t = {0};

    t.ffmpeg = proc_which("ffmpeg");
    t.ffprobe = proc_which("ffprobe");
    t.exiftool = proc_which("exiftool");

    if (!t.ffmpeg || !t.ffprobe) {
        fprintf(stderr, "error: %s not found on PATH\n", t.ffmpeg ? "ffprobe" : "ffmpeg");
        fprintf(stderr, "  install it with: %s\n", tools_install_hint("ffmpeg"));
        t.valid = 0;
        return t;
    }

    t.font = tools_find_font(font_override);

    if (!t.font && font_override && *font_override) {
        fprintf(stderr, "error: font not found: %s\n", font_override);
        t.valid = 0;
        return t;
    }

    if (!t.font) {
        fprintf(stderr, "error: no usable TrueType font found\n");
        fprintf(stderr, "  install one with: %s\n", tools_install_hint("font"));
        fprintf(stderr, "  or pass --font /path/to/font.ttf\n");
        t.valid = 0;
        return t;
    }

    t.valid = 1;
    return t;
}

void tools_free(tools_t *t) {
    free(t->ffmpeg);
    free(t->ffprobe);
    free(t->exiftool);
    free(t->font);
    memset(t, 0, sizeof(*t));
}
