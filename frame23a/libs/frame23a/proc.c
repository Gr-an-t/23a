#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "frame23a/fsutil.h"
#include "frame23a/proc.h"

static char *read_all_fd(int fd, size_t *out_len) {
    strbuf_t sb;
    strbuf_init(&sb);

    char chunk[4096];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        if (!strbuf_append(&sb, chunk, (size_t)n)) break;
    }

    if (!sb.data) {
        sb.data = calloc(1, 1);
        sb.len = 0;
    }

    *out_len = sb.len;
    return sb.data;
}

static char *read_all_stream(FILE *f, size_t *out_len) {
    fflush(f);
    rewind(f);

    strbuf_t sb;
    strbuf_init(&sb);

    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (!strbuf_append(&sb, chunk, n)) break;
    }

    if (!sb.data) {
        sb.data = calloc(1, 1);
        sb.len = 0;
    }

    *out_len = sb.len;
    return sb.data;
}

/*
 * execvp wants char *const[] while callers legitimately hold const strings.
 * Owning mutable duplicates satisfies both without casting away const.
 */
static char **argv_dup(const char *const argv[]) {
    size_t n = 0;
    while (argv[n]) n++;

    char **out = calloc(n + 1, sizeof(char *));
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        out[i] = fs_strdup(argv[i]);
        if (!out[i]) {
            for (size_t j = 0; j < i; j++) free(out[j]);
            free(out);
            return NULL;
        }
    }
    return out;
}

static void argv_free(char **argv) {
    if (!argv) return;
    for (size_t i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

int proc_run(const char *const argv[], int flags, proc_result_t *res) {
    memset(res, 0, sizeof(*res));
    res->exit_code = -1;

    char **child_argv = argv_dup(argv);
    if (!child_argv) return 0;

    int pipefd[2] = {-1, -1};
    if ((flags & PROC_CAPTURE_OUT) && pipe(pipefd) != 0) {
        argv_free(child_argv);
        return 0;
    }

    /*
     * stderr goes to a temp file rather than a second pipe: reading two pipes
     * sequentially can deadlock if the one we are not reading fills its buffer,
     * and a file has no such limit.
     */
    FILE *errf = NULL;
    if (flags & PROC_CAPTURE_ERR) {
        errf = tmpfile();
        if (!errf) {
            if (pipefd[0] >= 0) {
                close(pipefd[0]);
                close(pipefd[1]);
            }
            argv_free(child_argv);
            return 0;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (pipefd[0] >= 0) {
            close(pipefd[0]);
            close(pipefd[1]);
        }
        if (errf) fclose(errf);
        argv_free(child_argv);
        return 0;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);

        if (devnull >= 0) dup2(devnull, STDIN_FILENO);

        if (flags & PROC_CAPTURE_OUT) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        } else if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
        }

        if (errf) dup2(fileno(errf), STDERR_FILENO);

        if (devnull >= 0) close(devnull);

        execvp(child_argv[0], child_argv);
        _exit(127);
    }

    argv_free(child_argv);

    if (flags & PROC_CAPTURE_OUT) {
        close(pipefd[1]);
        res->out = read_all_fd(pipefd[0], &res->out_len);
        close(pipefd[0]);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        /* retry */
    }

    if (errf) {
        res->err = read_all_stream(errf, &res->err_len);
        fclose(errf);
    }

    res->ran = 1;
    if (WIFEXITED(status)) {
        res->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res->signalled = WTERMSIG(status);
        res->exit_code = -1;
    }

    return res->exit_code == 0;
}

void proc_result_free(proc_result_t *res) {
    free(res->out);
    free(res->err);
    res->out = NULL;
    res->err = NULL;
    res->out_len = 0;
    res->err_len = 0;
}

char *proc_which(const char *name) {
    if (strchr(name, '/')) {
        return access(name, X_OK) == 0 ? fs_strdup(name) : NULL;
    }

    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";

    char *copy = fs_strdup(path);
    if (!copy) return NULL;

    char *found = NULL;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        if (!*dir) dir = ".";

        char *cand = fs_path_join(dir, name);
        if (!cand) continue;

        if (access(cand, X_OK) == 0 && fs_is_file(cand)) {
            found = cand;
            break;
        }
        free(cand);
    }

    free(copy);
    return found;
}

void argv_reset(argv_t *a) {
    a->n = 0;
    a->overflow = 0;
    a->argv[0] = NULL;
}

void argv_add(argv_t *a, const char *s) {
    if (a->n >= PROC_ARGV_MAX - 1) {
        a->overflow = 1;
        return;
    }
    a->argv[a->n++] = s;
    a->argv[a->n] = NULL;
}

int proc_run_argv(argv_t *a, int flags, proc_result_t *res) {
    if (a->overflow) {
        memset(res, 0, sizeof(*res));
        res->exit_code = -1;
        return 0;
    }
    return proc_run(a->argv, flags, res);
}

pid_t proc_spawn(argv_t *a) {
    if (a->overflow) return -1;

    char **child_argv = argv_dup(a->argv);
    if (!child_argv) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        argv_free(child_argv);
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(child_argv[0], child_argv);
        _exit(127);
    }

    argv_free(child_argv);
    return pid;
}

void proc_reap_one(void) {
    int status = 0;
    while (wait(&status) < 0 && errno == EINTR) {
        /* retry */
    }
}

char *proc_argv_display(const argv_t *a) {
    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < a->n; i++) {
        if (i) strbuf_puts(&sb, " ");

        /* Quote anything a shell would mangle, so the line can be pasted. */
        if (strpbrk(a->argv[i], " \t'\"\\$`*?[]();&|<>#~!")) {
            strbuf_puts(&sb, "'");
            for (const char *p = a->argv[i]; *p; p++) {
                if (*p == '\'') {
                    strbuf_puts(&sb, "'\\''");
                } else {
                    strbuf_append(&sb, p, 1);
                }
            }
            strbuf_puts(&sb, "'");
        } else {
            strbuf_puts(&sb, a->argv[i]);
        }
    }

    return sb.data ? sb.data : fs_strdup("");
}

void proc_chomp(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}
