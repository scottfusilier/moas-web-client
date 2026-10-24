/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Included first by every .c file above the platform layer: the standard
 * types and a few compiler attributes.
 */

#ifndef MOAS_CORE_COMMON_H
#define MOAS_CORE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/err.h"

/* Number of elements in an array (not a pointer). */
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Marks a printf-style function, so that the compiler checks its format
 * string and arguments: fmt_index is the format's argument number (from 1),
 * first_arg the first value's.
 */
#if defined(__GNUC__) || defined(__clang__)
#define MOAS_PRINTF(fmt_index, first_arg)                                     \
    __attribute__((format(printf, fmt_index, first_arg)))
#else
#define MOAS_PRINTF(fmt_index, first_arg)
#endif

#endif /* MOAS_CORE_COMMON_H */
