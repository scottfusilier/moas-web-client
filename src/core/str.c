/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "core/str.h"

struct str
str_from_cstr(const char *s)
{
    return str_make(s, strlen(s));
}

bool
str_eq(struct str a, struct str b)
{
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static char
ascii_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

bool
str_eq_nocase(struct str a, struct str b)
{
    if (a.len != b.len) {
        return false;
    }
    for (size_t i = 0; i < a.len; i++) {
        if (ascii_lower(a.ptr[i]) != ascii_lower(b.ptr[i])) {
            return false;
        }
    }
    return true;
}

bool
str_starts_with(struct str s, struct str prefix)
{
    return s.len >= prefix.len && str_eq(str_make(s.ptr, prefix.len), prefix);
}

static bool
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

struct str
str_trim(struct str s)
{
    while (s.len > 0 && is_space(s.ptr[0])) {
        s.ptr++;
        s.len--;
    }
    while (s.len > 0 && is_space(s.ptr[s.len - 1])) {
        s.len--;
    }
    return s;
}

bool
str_split(struct str s, char sep, struct str *head, struct str *tail)
{
    const char *at = s.len > 0 ? memchr(s.ptr, sep, s.len) : NULL;
    if (at == NULL) {
        return false;
    }
    size_t before = (size_t)(at - s.ptr);
    *head = str_make(s.ptr, before);
    *tail = str_make(at + 1, s.len - before - 1);
    return true;
}

/*
 * The length of the valid UTF-8 sequence at s[0..len), or 0 if it is not
 * one: overlong forms, surrogates (U+D800 to U+DFFF) and values above
 * U+10FFFF are not valid.  *cp receives the code point.
 */
static size_t
utf8_sequence(const unsigned char *s, size_t len, uint32_t *cp)
{
    unsigned char b = s[0];
    size_t n;
    uint32_t value;
    uint32_t min;

    if (b < 0x80) {
        *cp = b;
        return 1;
    } else if (b >= 0xc2 && b <= 0xdf) {
        n = 2;
        value = b & 0x1fu;
        min = 0x80;
    } else if (b >= 0xe0 && b <= 0xef) {
        n = 3;
        value = b & 0x0fu;
        min = 0x800;
    } else if (b >= 0xf0 && b <= 0xf4) {
        n = 4;
        value = b & 0x07u;
        min = 0x10000;
    } else {
        return 0;
    }
    if (len < n) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xc0u) != 0x80u) {
            return 0;
        }
        value = (value << 6) | (s[i] & 0x3fu);
    }
    if (value < min || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu))
    {
        return 0;
    }
    *cp = value;
    return n;
}

int
str_copy_clean(struct str s, char *dst, size_t size)
{
    if (size == 0) {
        return ERR_LIMIT;
    }
    const unsigned char *p = (const unsigned char *)s.ptr;
    size_t out = 0;
    size_t i = 0;
    while (i < s.len) {
        uint32_t cp = 0;
        size_t n = utf8_sequence(p + i, s.len - i, &cp);
        bool control = cp < 0x20u || cp == 0x7fu ||
                       (cp >= 0x80u && cp <= 0x9fu);
        if (n == 0 || control) {
            /* Replace one bad byte, or one whole control character. */
            n = n == 0 ? 1 : n;
            if (out + 1 >= size) {
                dst[0] = '\0';
                return ERR_LIMIT;
            }
            dst[out++] = '?';
        } else {
            if (out + n >= size) {
                dst[0] = '\0';
                return ERR_LIMIT;
            }
            memcpy(dst + out, p + i, n);
            out += n;
        }
        i += n;
    }
    dst[out] = '\0';
    return 0;
}

int
str_to_i64(struct str s, int64_t min, int64_t max, int64_t *out)
{
    size_t i = 0;
    bool negative = false;

    if (s.len > 0 && s.ptr[0] == '-') {
        negative = true;
        i = 1;
    }
    if (i == s.len) {
        return ERR_INVALID;
    }
    /*
     * Accumulate the magnitude as unsigned, which can hold the magnitude of
     * INT64_MIN, and give up as soon as it cannot be any int64_t.
     */
    const uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : INT64_MAX;
    uint64_t magnitude = 0;
    bool too_big = false;
    for (; i < s.len; i++) {
        char c = s.ptr[i];
        if (c < '0' || c > '9') {
            return ERR_INVALID;
        }
        uint64_t digit = (uint64_t)(c - '0');
        if (magnitude > (limit - digit) / 10u) {
            too_big = true; /* keep checking that the rest are digits */
        } else {
            magnitude = magnitude * 10u + digit;
        }
    }
    if (too_big) {
        return ERR_RANGE;
    }
    int64_t value;
    if (negative) {
        /* -(magnitude) without overflowing when it is 2^63. */
        value = magnitude == (uint64_t)INT64_MAX + 1u ? INT64_MIN
                                                      : -(int64_t)magnitude;
    } else {
        value = (int64_t)magnitude;
    }
    if (value < min || value > max) {
        return ERR_RANGE;
    }
    *out = value;
    return 0;
}
