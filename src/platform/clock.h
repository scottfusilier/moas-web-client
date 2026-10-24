/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Time: a monotonic clock for timers, and local time for log lines. */

#ifndef MOAS_PLATFORM_CLOCK_H
#define MOAS_PLATFORM_CLOCK_H

#include <stdint.h>

/*
 * Milliseconds since an arbitrary starting point.  Never goes backwards, and
 * is not affected by changes to the system clock.
 */
uint64_t plat_monotonic_ms(void);

struct plat_local_time {
    int year;  /* for example 2026 */
    int month; /* 1 to 12 */
    int day;   /* 1 to 31 */
    int hour;
    int minute;
    int second;
    int millisecond;
};

/* The current local date and time. */
void plat_local_time(struct plat_local_time *out);

/* Waits for at least the given number of milliseconds. */
void plat_sleep_ms(uint32_t ms);

#endif /* MOAS_PLATFORM_CLOCK_H */
