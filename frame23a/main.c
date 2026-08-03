#include <stdio.h>

#include "frame23a/arg_parser.h"
#include "frame23a/run.h"
#include "frame23a/tools.h"

int main(int argc, char *argv[]) {
    cli_args_t args = parse_args(argc, argv);

    if (!args.valid) {
        args_free(&args);
        return 2;
    }

    int rc;

    switch (args.cmd) {
        case CMD_HELP:
            print_usage(argv[0]);
            rc = 0;
            break;

        case CMD_VERSION:
            printf("frame23a %s\n", FRAME23A_VERSION);
            rc = 0;
            break;

        case CMD_CHECK_DEPS:
            rc = run_check_deps();
            break;

        default: {
            tools_t tools = tools_discover(args.font);
            if (!tools.valid) {
                tools_free(&tools);
                args_free(&args);
                return 1;
            }

            rc = (args.cmd == CMD_REMOVE_METADATA) ? run_remove_metadata(&args, &tools)
                                                   : run_sheet(&args, &tools);
            tools_free(&tools);
            break;
        }
    }

    args_free(&args);
    return rc;
}
