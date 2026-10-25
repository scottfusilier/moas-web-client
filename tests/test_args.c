/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "app/args.h"
#include "test.h"

static char error[256];

static int
parse(struct app_args *a, int argc, const char *const *argv)
{
    return app_args_parse(argc, (char **)(uintptr_t)argv, a, error,
                          sizeof(error));
}

#define PARSE(a, ...)                                                         \
    parse((a),                                                                \
          (int)ARRAY_LEN(((const char *[]){"moas-web-client", __VA_ARGS__})), \
          (const char *[]){"moas-web-client", __VA_ARGS__})

static void
test_defaults(void)
{
    struct app_args a;
    const char *argv[] = {"moas-web-client"};

    CHECK_INT_EQ(parse(&a, 1, argv), 0);
    CHECK(!a.watch && !a.help && !a.version);
    CHECK_MEM_EQ(a.server, strlen(a.server), "127.0.0.1:12059");
    CHECK_INT_EQ(a.station, 1);
    CHECK_INT_EQ(a.radio, 1);
    CHECK_INT_EQ(a.rows, 6);
    CHECK_INT_EQ(a.columns, 5);
    CHECK(a.config == NULL);
}

static void
test_forms(void)
{
    struct app_args a;

    /* One dash, two dashes, and name=value all work. */
    CHECK_INT_EQ(PARSE(&a, "-watch", "-server", "192.168.1.10:12059",
                       "--radio=2", "-station", "3", "--rows", "4",
                       "-columns=8", "-config", "C:\\moas\\r\xc3\xa9.json"),
                 0);
    CHECK(a.watch);
    CHECK_MEM_EQ(a.server, strlen(a.server), "192.168.1.10:12059");
    CHECK_INT_EQ(a.radio, 2);
    CHECK_INT_EQ(a.station, 3);
    CHECK_INT_EQ(a.rows, 4);
    CHECK_INT_EQ(a.columns, 8);
    CHECK_MEM_EQ(a.config, strlen(a.config), "C:\\moas\\r\xc3\xa9.json");

    CHECK_INT_EQ(PARSE(&a, "-watch=false", "--help", "-version=true"), 0);
    CHECK(!a.watch && a.help && a.version);
}

static void
test_errors(void)
{
    struct app_args a;

    CHECK_INT_EQ(PARSE(&a, "-bogus"), ERR_INVALID);
    CHECK(strstr(error, "unknown option \"-bogus\"") != NULL);
    CHECK_INT_EQ(PARSE(&a, "watch"), ERR_INVALID);
    CHECK(strstr(error, "unexpected argument") != NULL);
    CHECK_INT_EQ(PARSE(&a, "-"), ERR_INVALID);
    CHECK_INT_EQ(PARSE(&a, "-server"), ERR_INVALID);
    CHECK(strstr(error, "-server needs a value") != NULL);
    CHECK_INT_EQ(PARSE(&a, "-radio", "0"), ERR_INVALID);
    CHECK(strstr(error, "-radio needs a whole number from 1 to 255") != NULL);
    CHECK_INT_EQ(PARSE(&a, "-radio", "two"), ERR_INVALID);
    CHECK_INT_EQ(PARSE(&a, "-radio=1x"), ERR_INVALID);
    CHECK_INT_EQ(PARSE(&a, "-rows", "99999999999999999999"), ERR_INVALID);
    CHECK_INT_EQ(PARSE(&a, "-watch=yes"), ERR_INVALID);
    CHECK(strstr(error, "-watch takes true or false") != NULL);
    /* A prefix of an option is not the option. */
    CHECK_INT_EQ(PARSE(&a, "-wat"), ERR_INVALID);
}

static void
test_usage_lists_every_option(void)
{
    const char *usage = app_args_usage();
    const char *names[] = {"-watch",  "-server",  "-station",
                           "-radio",  "-rows",    "-columns",
                           "-config", "-version", "-help"};
    for (size_t i = 0; i < ARRAY_LEN(names); i++) {
        test_check(strstr(usage, names[i]) != NULL, names[i], __FILE__,
                   __LINE__);
    }
}

int
main(void)
{
    RUN(test_defaults);
    RUN(test_forms);
    RUN(test_errors);
    RUN(test_usage_lists_every_option);
    return test_summary();
}
