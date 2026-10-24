/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "app/args.h"
#include "core/str.h"

enum kind {
    FLAG,   /* yes/no */
    NUMBER, /* an int within min..max */
    TEXT,
};

struct option {
    const char *name;
    enum kind kind;
    int min;
    int max;
};

/*
 * The options, in the order -help lists them.  Keep app_args_usage() in
 * step with this table.
 */
static const struct option options[] = {
    {"help", FLAG, 0, 0},        {"version", FLAG, 0, 0},
    {"watch", FLAG, 0, 0},       {"server", TEXT, 0, 0},
    {"station", NUMBER, 1, 255}, {"radio", NUMBER, 1, 255},
    {"rows", NUMBER, 1, 64},     {"columns", NUMBER, 1, 64},
    {"config", TEXT, 0, 0},
};

const char *
app_args_usage(void)
{
    return "Usage: moasweb [options]\n"
           "\n"
           "  -watch            connect to the MOAS server and print what it\n"
           "                    sends, changing nothing (for "
           "troubleshooting)\n"
           "  -server ADDRESS   the MOAS server, for example "
           "192.168.1.10:12059\n"
           "                    (default 127.0.0.1:12059)\n"
           "  -station N        the station number (default 1)\n"
           "  -radio N          the radio number to watch (default 1)\n"
           "  -rows N           grid rows to ask the server for (default 6)\n"
           "  -columns N        grid columns to ask the server for (default "
           "5)\n"
           "  -config FILE      the settings file (not used yet)\n"
           "  -version          print the version\n"
           "  -help             print this help\n"
           "\n"
           "Options may also be written --name, or -name=value.\n";
}

static const struct option *
find(struct str name)
{
    for (size_t i = 0; i < ARRAY_LEN(options); i++) {
        if (str_eq(name, str_from_cstr(options[i].name))) {
            return &options[i];
        }
    }
    return NULL;
}

/* Stores a value for option o in *out. */
static int
store(struct app_args *out, const struct option *o, const char *value,
      char *error, size_t error_size)
{
    const char *n = o->name;

    if (o->kind == FLAG) {
        bool on;
        if (value == NULL || strcmp(value, "true") == 0) {
            on = true;
        } else if (strcmp(value, "false") == 0) {
            on = false;
        } else {
            snprintf(error, error_size, "-%s takes true or false, not \"%s\"",
                     n, value);
            return ERR_INVALID;
        }
        if (strcmp(n, "help") == 0) {
            out->help = on;
        } else if (strcmp(n, "version") == 0) {
            out->version = on;
        } else {
            out->watch = on;
        }
        return 0;
    }

    if (o->kind == TEXT) {
        if (strcmp(n, "server") == 0) {
            out->server = value;
        } else {
            out->config = value;
        }
        return 0;
    }

    int64_t v = 0;
    int err = str_to_i64(str_from_cstr(value), o->min, o->max, &v);
    if (err != 0) {
        snprintf(error, error_size,
                 "-%s needs a whole number from %d to %d, "
                 "not \"%s\"",
                 n, o->min, o->max, value);
        return ERR_INVALID;
    }
    if (strcmp(n, "station") == 0) {
        out->station = (int)v;
    } else if (strcmp(n, "radio") == 0) {
        out->radio = (int)v;
    } else if (strcmp(n, "rows") == 0) {
        out->rows = (int)v;
    } else {
        out->columns = (int)v;
    }
    return 0;
}

int
app_args_parse(int argc, char **argv, struct app_args *out, char *error,
               size_t error_size)
{
    memset(out, 0, sizeof(*out));
    out->server = "127.0.0.1:12059";
    out->station = 1;
    out->radio = 1;
    out->rows = 6;
    out->columns = 5;
    error[0] = '\0';

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0') {
            snprintf(error, error_size, "unexpected argument \"%s\"", arg);
            return ERR_INVALID;
        }
        /* One dash or two. */
        const char *p = arg[1] == '-' ? arg + 2 : arg + 1;
        const char *eq = strchr(p, '=');
        struct str name = str_make(p,
                                   eq != NULL ? (size_t)(eq - p) : strlen(p));
        const struct option *o = find(name);
        if (o == NULL) {
            snprintf(error, error_size, "unknown option \"%s\"", arg);
            return ERR_INVALID;
        }

        const char *value = eq != NULL ? eq + 1 : NULL;
        if (value == NULL && o->kind != FLAG) {
            if (i + 1 >= argc) {
                snprintf(error, error_size, "-%s needs a value", o->name);
                return ERR_INVALID;
            }
            value = argv[++i];
        }
        int err = store(out, o, value, error, error_size);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}
