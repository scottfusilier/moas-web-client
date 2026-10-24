/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include <errno.h>
#include <time.h>

#include "platform/clock.h"

uint64_t
plat_monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void
plat_local_time(struct plat_local_time *out)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    out->year = tm.tm_year + 1900;
    out->month = tm.tm_mon + 1;
    out->day = tm.tm_mday;
    out->hour = tm.tm_hour;
    out->minute = tm.tm_min;
    out->second = tm.tm_sec;
    out->millisecond = (int)(ts.tv_nsec / 1000000);
}

void
plat_sleep_ms(uint32_t ms)
{
    struct timespec want = {
        .tv_sec = (time_t)(ms / 1000u),
        .tv_nsec = (long)(ms % 1000u) * 1000000L,
    };
    struct timespec left;

    /* A signal can end the sleep early; sleep for the rest. */
    while (nanosleep(&want, &left) != 0 && errno == EINTR) {
        want = left;
    }
}
