/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Read-only slices of text: a pointer and a length, not NUL-terminated and
 * not owned.  Untrusted input is handled as slices until it has been
 * checked.  A slice is only valid while the memory it points into is.
 */

#ifndef MOAS_CORE_STR_H
#define MOAS_CORE_STR_H

#include "core/common.h"

struct str {
    const char *ptr;
    size_t len;
};

/* A slice of a string literal, without its NUL. */
#define STR_LIT(s) ((struct str){"" s, sizeof(s) - 1})

static inline struct str
str_make(const char *ptr, size_t len)
{
    return (struct str){ptr, len};
}

/* A slice of a NUL-terminated string. */
struct str str_from_cstr(const char *s);

bool str_eq(struct str a, struct str b);

/* Equal, treating ASCII letters as the same in either case. */
bool str_eq_nocase(struct str a, struct str b);

bool str_starts_with(struct str s, struct str prefix);

/* Without leading and trailing spaces, tabs, CRs and LFs. */
struct str str_trim(struct str s);

/*
 * Splits s at the first sep: *head is what comes before it, *tail what
 * comes after.  Returns false, leaving both alone, if there is no sep.
 */
bool str_split(struct str s, char sep, struct str *head, struct str *tail);

/*
 * Parses a decimal integer: an optional '-' followed by one or more digits,
 * nothing else (no spaces, no '+', no leading "0x").  Returns ERR_INVALID
 * for anything else, and ERR_RANGE if the value is outside min..max.
 */
int str_to_i64(struct str s, int64_t min, int64_t max, int64_t *out);

/*
 * Copies s into dst (size bytes, always NUL-terminated) as clean text:
 * anything that could act as a terminal or control code becomes '?'.  That
 * is bytes that are not valid UTF-8, control characters (below 0x20), DEL,
 * and the C1 control characters U+0080 to U+009F, which some terminals
 * treat as escape sequences.  Returns ERR_LIMIT, leaving dst empty, if the
 * text does not fit.
 */
int str_copy_clean(struct str s, char *dst, size_t size);

#endif /* MOAS_CORE_STR_H */
