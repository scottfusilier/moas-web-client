/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moasweb's command line.  Options look like the Go version's: -name value
 * or -name=value, and --name works the same.  A yes/no option is turned on
 * by naming it (-watch), or set with -watch=true or -watch=false.
 */

#ifndef MOAS_APP_ARGS_H
#define MOAS_APP_ARGS_H

#include "core/common.h"

struct app_args {
    bool help;
    bool version;
    bool watch;
    const char *server; /* "127.0.0.1:12059" unless given */
    int station;        /* 1 unless given */
    int radio;          /* 1 unless given */
    int rows;           /* 6 unless given */
    int columns;        /* 5 unless given */
    const char *config; /* NULL unless given */
};

/*
 * Parses argv[1..argc).  The strings in *out point into argv.  On an error,
 * returns ERR_INVALID and writes a message for the user into error.
 */
int app_args_parse(int argc, char **argv, struct app_args *out, char *error,
                   size_t error_size);

/* The text shown by -help. */
const char *app_args_usage(void);

#endif /* MOAS_APP_ARGS_H */
