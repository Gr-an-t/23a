#ifndef FRAME23A_ARG_PARSER_H
#define FRAME23A_ARG_PARSER_H

typedef struct {
    int opt_i;
    int opt_l;
    int opt_r;
    int opt_x;
    char *filename;
    int valid;
} cli_args_t;

cli_args_t parse_args(int argc, char *argv[]);

#endif
