/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include "freq/rigctld_reply.h"

int
rigctld_parse_reply(struct str line, int *khz, int *report)
{
    struct str s = str_trim(line);

    if (str_starts_with(s, STR_LIT("RPRT "))) {
        int64_t code = 0;
        struct str rest = str_trim(str_make(s.ptr + 5, s.len - 5));
        if (str_to_i64(rest, -100000, 100000, &code) != 0) {
            return ERR_INVALID;
        }
        *report = (int)code;
        return ERR_IO;
    }

    /* Hz: digits, then optionally '.' and more digits, which are dropped. */
    struct str whole = s;
    struct str fraction = str_make(NULL, 0);
    str_split(s, '.', &whole, &fraction);
    for (size_t i = 0; i < fraction.len; i++) {
        if (fraction.ptr[i] < '0' || fraction.ptr[i] > '9') {
            return ERR_INVALID;
        }
    }
    int64_t hz = 0;
    int err = str_to_i64(whole, 0, INT64_MAX, &hz);
    if (err != 0) {
        return err;
    }
    if (hz < 1000 || hz > 1000000000000LL) {
        return ERR_RANGE;
    }
    *khz = (int)(hz / 1000);
    return 0;
}
