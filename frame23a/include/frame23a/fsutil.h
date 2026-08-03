#ifndef FRAME23A_FSUTIL_H
#define FRAME23A_FSUTIL_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

void strbuf_init(strbuf_t *sb);
void strbuf_free(strbuf_t *sb);
int strbuf_append(strbuf_t *sb, const char *s, size_t n);
int strbuf_puts(strbuf_t *sb, const char *s);
int strbuf_printf(strbuf_t *sb, const char *fmt, ...);

char *fs_strdup(const char *s);
char *fs_path_join(const char *dir, const char *name);
char *fs_dirname_dup(const char *path);
char *fs_basename_dup(const char *path);
char *fs_stem_dup(const char *path);
const char *fs_ext(const char *path);
int fs_ext_is(const char *path, const char *const *exts);

int fs_is_dir(const char *path);
int fs_is_file(const char *path);
off_t fs_file_size(const char *path);
int fs_mkdir_p(const char *path);
int fs_write_file(const char *path, const char *data, size_t len);
unsigned char *fs_read_file(const char *path, size_t *len);
int fs_copy_file(const char *src, const char *dst);

char *fs_tempdir_create(void);
void fs_rmtree(const char *path);

void fs_human_size(off_t bytes, char *buf, size_t n);
void fs_hms(double seconds, char *buf, size_t n);
void fs_hms_short(double seconds, char *buf, size_t n);

#endif
