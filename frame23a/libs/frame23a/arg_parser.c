#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame23a/arg_parser.h"

#define DEFAULT_WIDTH 1600
#define DEFAULT_JOBS 4

enum {
    OPT_PER_PAGE = 1000,
    OPT_NO_TIMESTAMPS,
    OPT_FONT,
    OPT_IN_PLACE,
    OPT_DRY_RUN,
    OPT_FLAT,
};

void print_usage(const char *prog) {
    fprintf(stderr,
        "frame23a " FRAME23A_VERSION " - contact sheet generator\n"
        "\n"
        "Usage:\n"
        "  %s [sheet] [options] [file|folder]...\n"
        "  %s remove-metadata [options] [file|folder]...\n"
        "  %s check-deps\n"
        "\n"
        "Generates a PNG contact sheet per video, and one paginated sheet per\n"
        "folder of images. Output is named after the source, minus its extension.\n"
        "With no path given, works on the current directory.\n"
        "\n"
        "Sheet options:\n"
        "  -o, --output DIR    Destination root (default: contact_sheets/ one\n"
        "                      level up from the input's own directory)\n"
        "  -n, --count N       Frames per video sheet (default: from duration)\n"
        "  -c, --columns N     Force column count (default: from tile aspect)\n"
        "  -w, --width PX      Target sheet width (default: %d)\n"
        "  -m, --min-tile PX   Never shrink a tile below this; the sheet grows\n"
        "                      instead (default: 320 video, 260 image)\n"
        "      --per-page N    Max images per sheet before paginating\n"
        "      --no-timestamps Omit the bottom-right timestamp overlay\n"
        "      --font PATH     TrueType font (default: auto-discovered)\n"
        "  -j, --jobs N        Parallel frame extractions (default: %d)\n"
        "\n"
        "remove-metadata options:\n"
        "  -o, --output DIR    Where to write cleaned copies\n"
        "      --in-place      Overwrite originals, after verifying the scrubbed\n"
        "                      file still decodes\n"
        "\n"
        "Common options:\n"
        "      --flat          Only the named folder; do not descend into\n"
        "                      subfolders (default is to include them)\n"
        "  -R, --recursive     Descend into subfolders (now the default; kept\n"
        "                      so existing commands keep working)\n"
        "  -f, --file PATH     Add an input path (positional args work too)\n"
        "      --dry-run       Report what would be written, write nothing\n"
        "  -v, --verbose       Show each command as it runs\n"
        "  -q, --quiet         Errors only\n"
        "  -h, --help          This message\n"
        "  -V, --version       Version\n",
        prog, prog, prog, DEFAULT_WIDTH, DEFAULT_JOBS);
}

static int parse_positive(const char *val, const char *flag, int *out) {
    char *end = NULL;
    long v = strtol(val, &end, 10);

    if (end == val || *end != '\0' || v <= 0 || v > 100000) {
        fprintf(stderr, "error: %s expects a positive integer, got '%s'\n", flag, val);
        return 0;
    }

    *out = (int)v;
    return 1;
}

static command_t match_command(const char *s, int *matched) {
    *matched = 1;

    if (strcmp(s, "sheet") == 0) return CMD_SHEET;
    if (strcmp(s, "remove-metadata") == 0) return CMD_REMOVE_METADATA;
    if (strcmp(s, "check-deps") == 0) return CMD_CHECK_DEPS;
    if (strcmp(s, "help") == 0) return CMD_HELP;
    if (strcmp(s, "version") == 0) return CMD_VERSION;

    *matched = 0;
    return CMD_SHEET;
}

/* True when this token is an option that consumes the following argument, so
 * a subcommand scan does not mistake an option's value for a command name. */
static int takes_value(const char *arg) {
    static const char *const with_value[] = {
        "--output", "--count", "--columns", "--width", "--min-tile",
        "--jobs", "--file", "--per-page", "--font", NULL,
    };

    if (arg[0] != '-' || arg[1] == '\0') return 0;

    if (arg[1] == '-') {
        if (strchr(arg, '=')) return 0; /* --output=DIR carries its own value */
        for (int i = 0; with_value[i]; i++) {
            if (strcmp(arg, with_value[i]) == 0) return 1;
        }
        return 0;
    }

    /* A short-option cluster consumes the next token only if the value-taking
     * letter is last, e.g. "-qo DIR" does but "-on 4" does not. */
    char last = arg[strlen(arg) - 1];
    return strchr("oncwmjf", last) != NULL;
}

/*
 * Subcommands are conventionally written first, but "frame23a -q
 * remove-metadata x.jpg" reads naturally too. Without this scan that spelling
 * silently fell through to `sheet` and generated contact sheets for someone
 * who asked to strip metadata, so the word is honoured wherever it appears.
 */
static int find_command(int argc, char *argv[], command_t *cmd) {
    for (int i = 1; i < argc; i++) {
        if (takes_value(argv[i])) {
            i++;
            continue;
        }
        if (argv[i][0] == '-') continue;

        int matched = 0;
        command_t c = match_command(argv[i], &matched);
        if (matched) {
            *cmd = c;
            return i;
        }

        /* First bare word is an input path: stop, so a file that happens to be
         * named "help" later on is never mistaken for a command. */
        return -1;
    }

    return -1;
}

cli_args_t parse_args(int argc, char *argv[]) {
    cli_args_t args = {0};
    args.cmd = CMD_SHEET;
    args.width = DEFAULT_WIDTH;
    args.jobs = DEFAULT_JOBS;

    /* Pointing at a folder means every picture under it, not just the ones
     * that happen to sit at the top level. --flat opts out. */
    args.recursive = 1;

    args.paths = calloc((size_t)argc + 1, sizeof(char *));
    if (!args.paths) {
        fprintf(stderr, "error: out of memory\n");
        return args;
    }

    int cmd_idx = find_command(argc, argv, &args.cmd);

    if (args.cmd == CMD_HELP || args.cmd == CMD_VERSION || args.cmd == CMD_CHECK_DEPS) {
        args.valid = 1;
        return args;
    }

    /* Filtered copy with the subcommand word removed, so getopt sees only
     * options and paths regardless of where the word appeared. */
    char **av = calloc((size_t)argc + 1, sizeof(char *));
    if (!av) {
        fprintf(stderr, "error: out of memory\n");
        return args;
    }

    int ac = 0;
    for (int i = 0; i < argc; i++) {
        if (i == cmd_idx) continue;
        av[ac++] = argv[i];
    }

    static const struct option longopts[] = {
        {"output",        required_argument, NULL, 'o'},
        {"count",         required_argument, NULL, 'n'},
        {"columns",       required_argument, NULL, 'c'},
        {"width",         required_argument, NULL, 'w'},
        {"min-tile",      required_argument, NULL, 'm'},
        {"jobs",          required_argument, NULL, 'j'},
        {"file",          required_argument, NULL, 'f'},
        {"recursive",     no_argument,       NULL, 'R'},
        {"flat",          no_argument,       NULL, OPT_FLAT},
        {"verbose",       no_argument,       NULL, 'v'},
        {"quiet",         no_argument,       NULL, 'q'},
        {"help",          no_argument,       NULL, 'h'},
        {"version",       no_argument,       NULL, 'V'},
        {"per-page",      required_argument, NULL, OPT_PER_PAGE},
        {"no-timestamps", no_argument,       NULL, OPT_NO_TIMESTAMPS},
        {"font",          required_argument, NULL, OPT_FONT},
        {"in-place",      no_argument,       NULL, OPT_IN_PLACE},
        {"dry-run",       no_argument,       NULL, OPT_DRY_RUN},
        {NULL, 0, NULL, 0},
    };

    optind = 1;
    opterr = 0;

    int opt;
    while ((opt = getopt_long(ac, av, ":o:n:c:w:m:j:f:RvqhV", longopts, NULL)) != -1) {
        switch (opt) {
            case 'o': args.output = optarg; break;
            case 'f': args.paths[args.path_count++] = optarg; break;
            case 'R': args.recursive = 1; break;
            case OPT_FLAT: args.recursive = 0; break;
            case 'v': args.verbose = 1; break;
            case 'q': args.quiet = 1; break;
            case OPT_NO_TIMESTAMPS: args.no_timestamps = 1; break;
            case OPT_FONT: args.font = optarg; break;
            case OPT_IN_PLACE: args.in_place = 1; break;
            case OPT_DRY_RUN: args.dry_run = 1; break;

            case 'h': args.cmd = CMD_HELP; args.valid = 1; goto done;
            case 'V': args.cmd = CMD_VERSION; args.valid = 1; goto done;

            case 'n': if (!parse_positive(optarg, "--count", &args.count)) goto done; break;
            case 'c': if (!parse_positive(optarg, "--columns", &args.columns)) goto done; break;
            case 'w': if (!parse_positive(optarg, "--width", &args.width)) goto done; break;
            case 'm': if (!parse_positive(optarg, "--min-tile", &args.min_tile)) goto done; break;
            case 'j': if (!parse_positive(optarg, "--jobs", &args.jobs)) goto done; break;
            case OPT_PER_PAGE:
                if (!parse_positive(optarg, "--per-page", &args.per_page)) goto done;
                break;

            case ':':
                fprintf(stderr, "error: option -%c requires a value\n", optopt);
                print_usage(argv[0]);
                goto done;
            case '?':
                if (optopt) {
                    fprintf(stderr, "error: unknown option -%c\n", optopt);
                } else {
                    fprintf(stderr, "error: unknown option %s\n", av[optind - 1]);
                }
                print_usage(argv[0]);
                goto done;
        }
    }

    for (int i = optind; i < ac; i++) {
        args.paths[args.path_count++] = av[i];
    }

    if (args.in_place && args.cmd != CMD_REMOVE_METADATA) {
        fprintf(stderr, "error: --in-place only applies to remove-metadata\n");
        goto done;
    }

    if (args.path_count == 0) {
        /*
         * Bare `frame23a` works on the current directory, the way `ls` does.
         * The one exception is an in-place scrub: rewriting every media file
         * around you is irreversible, and too severe to trigger by typing
         * nothing at all.
         */
        if (args.in_place) {
            fprintf(stderr, "error: --in-place needs an explicit path\n");
            fprintf(stderr, "  use '.' if you really mean every media file in this folder\n");
            goto done;
        }

        static char cwd_default[] = ".";
        args.paths[args.path_count++] = cwd_default;
    }

    if (args.quiet) args.verbose = 0;

    args.valid = 1;

done:
    free(av);
    return args;
}

void args_free(cli_args_t *args) {
    free(args->paths);
    args->paths = NULL;
    args->path_count = 0;
}
