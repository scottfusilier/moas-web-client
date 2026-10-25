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
    enum app_option id;
    const char *name;
    enum kind kind;
    int min;
    int max;
};

/* Keep app_args_usage() in step with this table. */
static const struct option options[] = {
    {APP_OPT_HELP, "help", FLAG, 0, 0},
    {APP_OPT_VERSION, "version", FLAG, 0, 0},
    {APP_OPT_WATCH, "watch", FLAG, 0, 0},
    {APP_OPT_CONFIG, "config", TEXT, 0, 0},
    {APP_OPT_SERVER, "server", TEXT, 0, 0},
    {APP_OPT_LISTEN, "listen", TEXT, 0, 0},
    {APP_OPT_STATION, "station", NUMBER, 1, 255},
    {APP_OPT_RADIOS, "radios", TEXT, 0, 0},
    {APP_OPT_RADIO, "radio", NUMBER, 1, 255},
    {APP_OPT_ROWS, "rows", NUMBER, 1, 64},
    {APP_OPT_COLUMNS, "columns", NUMBER, 1, 64},
};

_Static_assert(ARRAY_LEN(options) == APP_OPT_COUNT,
               "every option needs a row in options[]");

const char *
app_args_usage(void)
{
    return "Usage: moas-web-client [options]\n"
           "\n"
           "Serves a page with the MOAS server's antenna buttons; open the "
           "address it\n"
           "prints in a browser.  Options given here win over the settings "
           "file.\n"
           "\n"
           "  -config FILE      the settings file; without it, "
           "moas-web-client.json\n"
           "                    beside the program if there is one, else "
           "in your\n"
           "                    settings folder\n"
           "  -server ADDRESS   the MOAS server, for example "
           "192.168.1.10:12059\n"
           "  -listen ADDRESS   where to serve the page (default "
           "127.0.0.1:8080, which\n"
           "                    reaches this computer only)\n"
           "  -station N        the station number (default 1)\n"
           "  -radios LIST      the radios to show, such as 1,2; not with a "
           "settings file\n"
           "                    that lists radios\n"
           "  -rows N           grid rows to ask the server for (default 6)\n"
           "  -columns N        grid columns to ask the server for (default "
           "5)\n"
           "\n"
           "  -watch            instead, print what the MOAS server sends for "
           "one radio,\n"
           "                    changing nothing (for troubleshooting)\n"
           "  -radio N          the radio to watch (default 1)\n"
           "\n"
           "  -version          print the version\n"
           "  -help             print this help\n"
           "\n"
           "Options may also be written --name, or -name=value.\n";
}

const char *
app_args_name(enum app_option option)
{
    for (size_t i = 0; i < ARRAY_LEN(options); i++) {
        if (options[i].id == option) {
            return options[i].name;
        }
    }
    return "";
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
    if (o->kind == FLAG) {
        bool on;
        if (value == NULL || strcmp(value, "true") == 0) {
            on = true;
        } else if (strcmp(value, "false") == 0) {
            on = false;
        } else {
            snprintf(error, error_size, "-%s takes true or false, not \"%s\"",
                     o->name, value);
            return ERR_INVALID;
        }
        if (o->id == APP_OPT_HELP) {
            out->help = on;
        } else if (o->id == APP_OPT_VERSION) {
            out->version = on;
        } else {
            out->watch = on;
        }
        return 0;
    }

    if (o->kind == TEXT) {
        switch (o->id) {
        case APP_OPT_CONFIG:
            out->config = value;
            break;
        case APP_OPT_SERVER:
            out->server = value;
            break;
        case APP_OPT_LISTEN:
            out->listen = value;
            break;
        default:
            out->radios = value;
            break;
        }
        return 0;
    }

    int64_t v = 0;
    int err = str_to_i64(str_from_cstr(value), o->min, o->max, &v);
    if (err != 0) {
        snprintf(error, error_size,
                 "-%s needs a whole number from %d to %d, not \"%s\"", o->name,
                 o->min, o->max, value);
        return ERR_INVALID;
    }
    switch (o->id) {
    case APP_OPT_STATION:
        out->station = (int)v;
        break;
    case APP_OPT_RADIO:
        out->radio = (int)v;
        break;
    case APP_OPT_ROWS:
        out->rows = (int)v;
        break;
    default:
        out->columns = (int)v;
        break;
    }
    return 0;
}

int
app_args_parse(int argc, char **argv, struct app_args *out, char *error,
               size_t error_size)
{
    memset(out, 0, sizeof(*out));
    out->server = "127.0.0.1:12059";
    out->listen = "127.0.0.1:8080";
    out->station = 1;
    out->radios = "1,2";
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
        out->given[o->id] = true;
    }
    return 0;
}
