/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "core/caps.h"
#include "core/log.h"
#include "test.h"

static char last_line[CAPS_LOG_LINE + 16];
static int lines;

static void
capture(enum log_level level, const char *line, void *arg)
{
    (void)level;
    (void)arg;
    lines++;
    size_t n = strlen(line);
    if (n >= sizeof(last_line)) {
        n = sizeof(last_line) - 1;
    }
    memcpy(last_line, line, n);
    last_line[n] = '\0';
}

/* The message part of the last line: after "date time LEVEL ". */
static const char *
message(void)
{
    const char *p = last_line;
    for (int spaces = 0; *p != '\0' && spaces < 3; p++) {
        if (*p == ' ') {
            spaces++;
        }
    }
    return p;
}

static void
test_format_and_level(void)
{
    lines = 0;
    log_info("connected to %s", "192.168.1.10:12059");
    CHECK_INT_EQ(lines, 1);
    /* "YYYY-MM-DD HH:MM:SS.mmm INFO " */
    CHECK(strlen(last_line) > 29);
    CHECK(last_line[4] == '-' && last_line[10] == ' ' && last_line[19] == '.');
    CHECK(strstr(last_line, " INFO ") != NULL);
    CHECK_MEM_EQ(message(), strlen(message()),
                 "connected to 192.168.1.10:12059");

    log_debug("hidden at the default level");
    CHECK_INT_EQ(lines, 1);
    log_set_level(LOG_LEVEL_DEBUG);
    log_debug("shown");
    CHECK_INT_EQ(lines, 2);
    log_set_level(LOG_LEVEL_ERROR);
    log_warn("hidden");
    log_error("shown");
    CHECK_INT_EQ(lines, 3);
    log_set_level(LOG_LEVEL_INFO);
}

static void
test_control_characters_are_replaced(void)
{
    /* Text from the network trying to forge a second line and an escape. */
    log_warn("name: %s", "N1MM\r\n2026-01-01 00:00:00.000 ERROR fake\x1b[2J");
    const char *m = message();
    CHECK(strchr(last_line, '\n') == NULL);
    CHECK(strchr(last_line, '\r') == NULL);
    CHECK(strchr(last_line, '\x1b') == NULL);
    CHECK_MEM_EQ(m, strlen(m),
                 "name: N1MM??2026-01-01 00:00:00.000 ERROR fake?[2J");
}

static void
test_long_line_is_cut_and_marked(void)
{
    char big[CAPS_LOG_LINE * 2];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    log_info("%s", big);
    size_t n = strlen(last_line);
    CHECK_INT_EQ(n, CAPS_LOG_LINE - 1);
    CHECK_MEM_EQ(last_line + n - 3, 3, "...");
}

int
main(void)
{
    log_set_sink(capture, NULL);
    RUN(test_format_and_level);
    RUN(test_control_characters_are_replaced);
    RUN(test_long_line_is_cut_and_marked);
    log_set_sink(NULL, NULL);
    return test_summary();
}
