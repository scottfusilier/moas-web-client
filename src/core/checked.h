/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Size arithmetic that cannot silently wrap around.  Each function stores
 * the result and returns true, or returns false if the result does not fit
 * in a size_t (and leaves *out alone).
 */

#ifndef MOAS_CORE_CHECKED_H
#define MOAS_CORE_CHECKED_H

#include "core/common.h"

static inline bool
checked_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static inline bool
checked_mul(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

#endif /* MOAS_CORE_CHECKED_H */
