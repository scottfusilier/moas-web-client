/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moasweb: a client for the MOAS II antenna switch server.
 *
 * So far only watch mode (-watch) exists; the web interface comes in later
 * stages (see docs/ROADMAP.md).
 */

#include "core/common.h"

#include <stdio.h>

#include "app/args.h"
#include "app/watch.h"
#include "platform/proc.h"

int
main(int argc, char **argv)
{
    if (plat_process_init(&argc, &argv) != 0) {
        fprintf(stderr, "moasweb: cannot read the command line\n");
        return 1;
    }

    struct app_args args;
    char error[256];
    if (app_args_parse(argc, argv, &args, error, sizeof(error)) != 0) {
        fprintf(stderr, "moasweb: %s\nRun moasweb -help for the options.\n",
                error);
        return 2;
    }
    if (args.help) {
        fputs(app_args_usage(), stdout);
        return 0;
    }
    if (args.version) {
        printf("moasweb %s\n", MOAS_VERSION);
        return 0;
    }
    if (args.watch) {
        return app_watch_run(&args);
    }
    printf("moasweb %s: the web interface is not built yet.  Run moasweb "
           "-watch to watch a MOAS server, or -help for the options.\n",
           MOAS_VERSION);
    return 0;
}
