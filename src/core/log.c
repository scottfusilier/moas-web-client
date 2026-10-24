/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/caps.h"
#include "core/log.h"
#include "platform/clock.h"

static enum log_level max_level = LOG_LEVEL_INFO;
static log_sink_fn *sink;
static void *sink_arg;

static const char *const level_names[] = {
    [LOG_LEVEL_ERROR] = "ERROR",
    [LOG_LEVEL_WARN] = "WARN",
    [LOG_LEVEL_INFO] = "INFO",
    [LOG_LEVEL_DEBUG] = "DEBUG",
};

void
log_set_level(enum log_level level)
{
    max_level = level;
}

void
log_set_sink(log_sink_fn *fn, void *arg)
{
    sink = fn;
    sink_arg = arg;
}

MOAS_PRINTF(2, 0)
static void
log_line(enum log_level level, const char *fmt, va_list ap)
{
    if (level > max_level) {
        return;
    }

    char line[CAPS_LOG_LINE];
    struct plat_local_time t;
    plat_local_time(&t);
    int head = snprintf(line, sizeof(line),
                        "%04d-%02d-%02d %02d:%02d:%02d.%03d %s ", t.year,
                        t.month, t.day, t.hour, t.minute, t.second,
                        t.millisecond, level_names[level]);
    if (head < 0 || (size_t)head >= sizeof(line)) {
        return;
    }
    size_t start = (size_t)head;
    int body = vsnprintf(line + start, sizeof(line) - start, fmt, ap);
    if (body < 0) {
        return;
    }
    size_t end = start + (size_t)body;
    if (end >= sizeof(line)) {
        /* Cut short: mark it. */
        end = sizeof(line) - 1;
        memcpy(line + end - 3, "...", 3);
    }
    line[end] = '\0';

    /* Only the message can hold untrusted text; clean it. */
    for (size_t i = start; i < end; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c < 0x20 || c == 0x7f) {
            line[i] = '?';
        }
    }

    if (sink != NULL) {
        sink(level, line, sink_arg);
    } else {
        fprintf(stderr, "%s\n", line);
        fflush(stderr);
    }
}

void
log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_line(LOG_LEVEL_ERROR, fmt, ap);
    va_end(ap);
}

void
log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_line(LOG_LEVEL_WARN, fmt, ap);
    va_end(ap);
}

void
log_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_line(LOG_LEVEL_INFO, fmt, ap);
    va_end(ap);
}

void
log_debug(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_line(LOG_LEVEL_DEBUG, fmt, ap);
    va_end(ap);
}
