#include <stdio.h>
#include <unistd.h>
#include "frame23a/arg_parser.h"

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-i] [-l] [-r] [-f file] [-x]\n"
        "  -i           Flag i\n"
        "  -l           Flag l\n"
        "  -r           Flag r\n"
        "  -f <file>    Input filename\n"
        "  -x           Flag x\n",
        prog
    );
}

cli_args_t parse_args(int argc, char *argv[]) {
    cli_args_t args = {0};  // zero-initialize
    int opt;

    while ((opt = getopt(argc, argv, ":ilrf:x")) != -1) {
        switch (opt) {
            case 'i': args.opt_i = 1; break;
            case 'l': args.opt_l = 1; break;
            case 'r': args.opt_r = 1; break;
            case 'x': args.opt_x = 1; break;
            case 'f': args.filename = optarg; break;
            case ':':
                fprintf(stderr, "Option -%c requires a value\n", optopt);
                print_usage(argv[0]);
                args.valid = 0;
                return args;
            case '?':
                fprintf(stderr, "Unknown option: -%c\n", optopt);
                print_usage(argv[0]);
                args.valid = 0;
                return args;
        }
    }

    args.valid = 1;
    return args;
}
