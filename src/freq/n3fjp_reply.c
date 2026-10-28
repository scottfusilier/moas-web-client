/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "freq/n3fjp_reply.h"

/* Where needle starts in s, or SIZE_MAX if it does not occur. */
static size_t
find(struct str s, struct str needle)
{
    for (size_t i = 0; needle.len <= s.len && i <= s.len - needle.len; i++) {
        if (memcmp(s.ptr + i, needle.ptr, needle.len) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

/* "14.025000" (MHz) to Hz; decimals after the sixth are dropped. */
static int
mhz_to_hz(struct str text, int64_t *hz)
{
    struct str whole = text;
    struct str fraction = str_make(NULL, 0);
    str_split(text, '.', &whole, &fraction);
    if (whole.len == 0 || whole.ptr[0] < '0' || whole.ptr[0] > '9') {
        return ERR_INVALID;
    }
    int64_t mhz = 0;
    int err = str_to_i64(whole, 0, INT64_MAX, &mhz);
    if (err != 0) {
        return err;
    }
    if (mhz > 1000000) {
        return ERR_RANGE;
    }
    int64_t micro = 0;
    for (size_t i = 0; i < fraction.len; i++) {
        char c = fraction.ptr[i];
        if (c < '0' || c > '9') {
            return ERR_INVALID;
        }
        if (i < 6) {
            micro = micro * 10 + (c - '0');
        }
    }
    for (size_t i = fraction.len; i < 6; i++) {
        micro *= 10;
    }
    *hz = mhz * 1000000 + micro;
    return 0;
}

int
n3fjp_parse_line(struct str line, int *khz)
{
    const struct str cmd_open = STR_LIT("<CMD>");
    const struct str cmd_close = STR_LIT("</CMD>");
    const struct str freq_open = STR_LIT("<FREQ>");
    const struct str freq_close = STR_LIT("</FREQ>");
    struct str s = str_trim(line);

    if (s.len == 0) {
        return ERR_NOT_FOUND;
    }
    if (s.len < cmd_open.len + cmd_close.len ||
        !str_starts_with(s, cmd_open) ||
        !str_eq(str_make(s.ptr + s.len - cmd_close.len, cmd_close.len),
                cmd_close))
    {
        return ERR_INVALID;
    }
    struct str body = str_make(s.ptr + cmd_open.len,
                               s.len - cmd_open.len - cmd_close.len);
    if (!str_starts_with(body, STR_LIT("<READBMFRESPONSE>"))) {
        return ERR_NOT_FOUND;
    }

    size_t at = find(body, freq_open);
    if (at == SIZE_MAX) {
        return ERR_INVALID;
    }
    struct str rest = str_make(body.ptr + at + freq_open.len,
                               body.len - at - freq_open.len);
    size_t end = find(rest, freq_close);
    if (end == SIZE_MAX) {
        return ERR_INVALID;
    }
    struct str value = str_trim(str_make(rest.ptr, end));
    if (value.len == 0) {
        return ERR_RANGE; /* no frequency */
    }

    int64_t hz = 0;
    int err = mhz_to_hz(value, &hz);
    if (err != 0) {
        return err;
    }
    if (hz < 1000 || hz > 1000000000000LL) {
        return ERR_RANGE;
    }
    *khz = (int)(hz / 1000);
    return 0;
}
