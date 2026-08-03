#ifndef FRAME23A_RUN_H
#define FRAME23A_RUN_H

#include "frame23a/arg_parser.h"
#include "frame23a/tools.h"

int run_sheet(const cli_args_t *args, const tools_t *tools);
int run_remove_metadata(const cli_args_t *args, const tools_t *tools);
int run_check_deps(void);

/* Resolves the default destination: contact_sheets/ one level above the
 * input's own directory. Caller frees. */
char *run_default_output(const char *input, const char *leaf);

#endif
