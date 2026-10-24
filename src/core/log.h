/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Logging.  Each message becomes one line, "2026-10-24 14:03:27.512 WARN
 * message", written to standard error.  Control characters in a message,
 * which could come from the network, are replaced with '?', so that a
 * message cannot forge extra lines or terminal escapes.  Lines longer than
 * CAPS_LOG_LINE are cut short and end in "...".
 */

#ifndef MOAS_CORE_LOG_H
#define MOAS_CORE_LOG_H

#include "core/common.h"

enum log_level {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
};

/* Messages less important than level are dropped.  The default:
 * LOG_LEVEL_INFO. */
void log_set_level(enum log_level level);

/*
 * Sends each finished line (without a newline) to fn instead of standard
 * error; NULL restores standard error.  For tests.
 */
typedef void log_sink_fn(enum log_level level, const char *line, void *arg);
void log_set_sink(log_sink_fn *fn, void *arg);

void log_error(const char *fmt, ...) MOAS_PRINTF(1, 2);
void log_warn(const char *fmt, ...) MOAS_PRINTF(1, 2);
void log_info(const char *fmt, ...) MOAS_PRINTF(1, 2);
void log_debug(const char *fmt, ...) MOAS_PRINTF(1, 2);

#endif /* MOAS_CORE_LOG_H */
