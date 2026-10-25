/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moas-web-client's command line.  Options look like -name value or
 * -name=value, and --name works the same.  A yes/no option is turned on by
 * naming it (-watch), or set with -watch=true or -watch=false.
 *
 * Options given on the command line win over the settings file; which ones
 * were given is recorded, so the page can say so.
 */

#ifndef MOAS_APP_ARGS_H
#define MOAS_APP_ARGS_H

#include "core/common.h"

/* Each option, for app_args_given(). */
enum app_option {
    APP_OPT_HELP,
    APP_OPT_VERSION,
    APP_OPT_WATCH,
    APP_OPT_CONFIG,
    APP_OPT_SERVER,
    APP_OPT_LISTEN,
    APP_OPT_STATION,
    APP_OPT_RADIOS,
    APP_OPT_RADIO,
    APP_OPT_ROWS,
    APP_OPT_COLUMNS,
    APP_OPT_COUNT,
};

struct app_args {
    bool help;
    bool version;
    bool watch;
    const char *config; /* NULL unless given */
    const char *server; /* "127.0.0.1:12059" unless given */
    const char *listen; /* "127.0.0.1:8080" unless given */
    int station;        /* 1 unless given */
    const char *radios; /* the radios to serve, "1,2" unless given */
    int radio;          /* watch mode's radio, 1 unless given */
    int rows;           /* 6 unless given */
    int columns;        /* 5 unless given */
    bool given[APP_OPT_COUNT];
};

/*
 * Parses argv[1..argc).  The strings in *out point into argv.  On an error,
 * returns ERR_INVALID and writes a message for the user into error.
 */
int app_args_parse(int argc, char **argv, struct app_args *out, char *error,
                   size_t error_size);

/* The option's name as typed, without the dash: "server". */
const char *app_args_name(enum app_option option);

/* The text shown by -help. */
const char *app_args_usage(void);

#endif /* MOAS_APP_ARGS_H */
