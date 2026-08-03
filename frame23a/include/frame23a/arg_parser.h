#ifndef FRAME23A_ARG_PARSER_H
#define FRAME23A_ARG_PARSER_H

#define FRAME23A_VERSION "0.1.0"

typedef enum {
    CMD_SHEET,
    CMD_REMOVE_METADATA,
    CMD_CHECK_DEPS,
    CMD_HELP,
    CMD_VERSION,
} command_t;

typedef struct {
    command_t cmd;

    char **paths; /* borrowed from argv; the array itself is owned */
    int path_count;

    const char *output;
    const char *font;

    int count;    /* -n; 0 = derive from duration */
    int columns;  /* -c; 0 = derive from tile aspect */
    int width;    /* -w target sheet width */
    int min_tile; /* -m; 0 = per-media default */
    int per_page; /* 0 = derive from max sheet height */
    int jobs;

    int recursive;
    int no_timestamps;
    int in_place;
    int dry_run;
    int verbose;
    int quiet;

    int valid;
} cli_args_t;

cli_args_t parse_args(int argc, char *argv[]);
void args_free(cli_args_t *args);
void print_usage(const char *prog);

#endif
