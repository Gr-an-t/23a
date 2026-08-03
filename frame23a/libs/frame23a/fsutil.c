#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "frame23a/fsutil.h"

void strbuf_init(strbuf_t *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_free(strbuf_t *sb) {
    free(sb->data);
    strbuf_init(sb);
}

static int strbuf_grow(strbuf_t *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 1;

    size_t cap = sb->cap ? sb->cap : 128;
    while (cap < need) cap *= 2;

    char *p = realloc(sb->data, cap);
    if (!p) return 0;

    sb->data = p;
    sb->cap = cap;
    return 1;
}

int strbuf_append(strbuf_t *sb, const char *s, size_t n) {
    if (!strbuf_grow(sb, n)) return 0;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int strbuf_puts(strbuf_t *sb, const char *s) {
    return strbuf_append(sb, s, strlen(s));
}

int strbuf_printf(strbuf_t *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;

    if (!strbuf_grow(sb, (size_t)n)) return 0;

    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);

    sb->len += (size_t)n;
    return 1;
}

char *fs_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *fs_path_join(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    while (dlen > 1 && dir[dlen - 1] == '/') dlen--;

    while (*name == '/') name++;
    size_t nlen = strlen(name);

    char *p = malloc(dlen + nlen + 2);
    if (!p) return NULL;

    memcpy(p, dir, dlen);
    size_t at = dlen;
    if (at == 0 || p[at - 1] != '/') p[at++] = '/';
    memcpy(p + at, name, nlen);
    p[at + nlen] = '\0';
    return p;
}

/* Trailing slashes are stripped first so dirname("/a/b/") is "/a", not "/a/b". */
char *fs_dirname_dup(const char *path) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;

    while (n > 0 && path[n - 1] != '/') n--;
    if (n == 0) return fs_strdup(".");

    while (n > 1 && path[n - 1] == '/') n--;

    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, path, n);
    p[n] = '\0';
    return p;
}

char *fs_basename_dup(const char *path) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;

    size_t start = n;
    while (start > 0 && path[start - 1] != '/') start--;

    size_t len = n - start;
    if (len == 0) return fs_strdup("/");

    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, path + start, len);
    p[len] = '\0';
    return p;
}

char *fs_stem_dup(const char *path) {
    char *base = fs_basename_dup(path);
    if (!base) return NULL;

    char *dot = strrchr(base, '.');
    if (dot && dot != base) *dot = '\0';
    return base;
}

const char *fs_ext(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return "";
    return dot + 1;
}

int fs_ext_is(const char *path, const char *const *exts) {
    const char *ext = fs_ext(path);
    if (!*ext) return 0;

    for (size_t i = 0; exts[i]; i++) {
        const char *a = ext;
        const char *b = exts[i];
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*a && !*b) return 1;
    }
    return 0;
}

int fs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int fs_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

off_t fs_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return st.st_size;
}

int fs_mkdir_p(const char *path) {
    char *tmp = fs_strdup(path);
    if (!tmp) return 0;

    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && !fs_is_dir(tmp)) {
            free(tmp);
            return 0;
        }
        *p = '/';
    }

    int ok = (mkdir(tmp, 0755) == 0 || fs_is_dir(tmp));
    free(tmp);
    return ok;
}

int fs_write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    int ok = (len == 0) || (fwrite(data, 1, len, f) == len);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

unsigned char *fs_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (!strbuf_append(&sb, chunk, n)) {
            strbuf_free(&sb);
            fclose(f);
            return NULL;
        }
    }

    int bad = ferror(f);
    fclose(f);

    if (bad) {
        strbuf_free(&sb);
        return NULL;
    }

    if (!sb.data) {
        sb.data = calloc(1, 1);
        sb.len = 0;
    }

    *len = sb.len;
    return (unsigned char *)sb.data;
}

int fs_copy_file(const char *src, const char *dst) {
    size_t len = 0;
    unsigned char *data = fs_read_file(src, &len);
    if (!data) return 0;

    int ok = fs_write_file(dst, (const char *)data, len);
    free(data);
    return ok;
}

char *fs_tempdir_create(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";

    char *tmpl = fs_path_join(base, "frame23a-XXXXXX");
    if (!tmpl) return NULL;

    if (!mkdtemp(tmpl)) {
        free(tmpl);
        return NULL;
    }
    return tmpl;
}

void fs_rmtree(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        unlink(path);
        return;
    }

    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;

        char *child = fs_path_join(path, e->d_name);
        if (!child) continue;

        if (fs_is_dir(child)) {
            fs_rmtree(child);
        } else {
            unlink(child);
        }
        free(child);
    }

    closedir(d);
    rmdir(path);
}

void fs_human_size(off_t bytes, char *buf, size_t n) {
    if (bytes < 0) {
        snprintf(buf, n, "unknown");
        return;
    }

    static const char *unit[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)bytes;
    int u = 0;

    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }

    if (u == 0) {
        snprintf(buf, n, "%.0f B", v);
    } else {
        snprintf(buf, n, "%.1f %s", v, unit[u]);
    }
}

void fs_hms(double seconds, char *buf, size_t n) {
    if (seconds < 0 || seconds != seconds) {
        snprintf(buf, n, "--:--:--");
        return;
    }

    long total = (long)(seconds + 0.5);
    snprintf(buf, n, "%02ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
}

/* Drops the hour field under an hour so tile overlays stay narrow. */
void fs_hms_short(double seconds, char *buf, size_t n) {
    if (seconds < 0 || seconds != seconds) {
        snprintf(buf, n, "--:--");
        return;
    }

    long total = (long)(seconds + 0.5);
    if (total >= 3600) {
        snprintf(buf, n, "%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
    } else {
        snprintf(buf, n, "%02ld:%02ld", total / 60, total % 60);
    }
}
