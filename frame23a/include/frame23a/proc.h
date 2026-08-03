#ifndef FRAME23A_PROC_H
#define FRAME23A_PROC_H

#include <stddef.h>
#include <sys/types.h>

#define PROC_CAPTURE_OUT 1
#define PROC_CAPTURE_ERR 2

typedef struct {
    int ran;
    int exit_code;
    int signalled;
    char *out;
    size_t out_len;
    char *err;
    size_t err_len;
} proc_result_t;

/*
 * Spawns argv[0] via fork/execvp. argv is passed straight through, so no shell
 * ever sees it and filenames containing quotes, spaces or semicolons are safe
 * without any escaping.
 */
int proc_run(const char *const argv[], int flags, proc_result_t *res);
void proc_result_free(proc_result_t *res);

#define PROC_ARGV_MAX 64

/* Fixed-capacity argv builder. Strings are borrowed, so their storage must
 * outlive the proc_run_argv call. */
typedef struct {
    const char *argv[PROC_ARGV_MAX];
    int n;
    int overflow;
} argv_t;

void argv_reset(argv_t *a);
void argv_add(argv_t *a, const char *s);
int proc_run_argv(argv_t *a, int flags, proc_result_t *res);

/* Renders the argv as a copy-pasteable shell line, for --verbose. */
char *proc_argv_display(const argv_t *a);

/*
 * Fire-and-forget spawn for running several children at once. Output is
 * discarded, so callers judge success by inspecting what the child produced.
 * Safe to reuse the argv buffers as soon as this returns: exec has already
 * taken its own copy.
 */
pid_t proc_spawn(argv_t *a);
void proc_reap_one(void);

char *proc_which(const char *name);

/* Trailing newline stripped; useful for one-line command output. */
void proc_chomp(char *s);

#endif
