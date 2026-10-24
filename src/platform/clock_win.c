/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include <windows.h>

#include "platform/clock.h"

uint64_t
plat_monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

void
plat_local_time(struct plat_local_time *out)
{
    SYSTEMTIME st;

    GetLocalTime(&st);
    out->year = st.wYear;
    out->month = st.wMonth;
    out->day = st.wDay;
    out->hour = st.wHour;
    out->minute = st.wMinute;
    out->second = st.wSecond;
    out->millisecond = st.wMilliseconds;
}

void
plat_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}
