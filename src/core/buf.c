/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/checked.h"

/* First allocation, so that small buffers do not grow a byte at a time. */
#define BUF_MIN_CAP 64

void
buf_init(struct buf *b, size_t max)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    /* Room for the NUL must also fit in a size_t. */
    b->max = max < SIZE_MAX ? max : SIZE_MAX - 1;
}

void
buf_fini(struct buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void
buf_clear(struct buf *b)
{
    b->len = 0;
    if (b->data != NULL) {
        b->data[0] = '\0';
    }
}

int
buf_reserve(struct buf *b, size_t extra)
{
    size_t want;
    if (!checked_add(b->len, extra, &want) || want > b->max) {
        return ERR_LIMIT;
    }
    want += 1; /* the NUL; cannot overflow, since max < SIZE_MAX */
    if (want <= b->cap) {
        return 0;
    }
    /* Double, but never beyond what max allows. */
    size_t cap = b->cap < BUF_MIN_CAP ? BUF_MIN_CAP : b->cap;
    while (cap < want) {
        if (!checked_mul(cap, 2, &cap)) {
            cap = want;
            break;
        }
    }
    if (cap > b->max + 1) {
        cap = b->max + 1;
    }
    char *data = realloc(b->data, cap);
    if (data == NULL) {
        return ERR_NOMEM;
    }
    if (b->data == NULL) {
        data[0] = '\0';
    }
    b->data = data;
    b->cap = cap;
    return 0;
}

int
buf_append(struct buf *b, const void *data, size_t len)
{
    int err = buf_reserve(b, len);
    if (err != 0) {
        return err;
    }
    if (len > 0) {
        memcpy(b->data + b->len, data, len);
    }
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

int
buf_append_str(struct buf *b, struct str s)
{
    return buf_append(b, s.ptr, s.len);
}

int
buf_append_cstr(struct buf *b, const char *s)
{
    return buf_append(b, s, strlen(s));
}

int
buf_vappendf(struct buf *b, const char *fmt, va_list ap)
{
    va_list again;

    /* First find out how long the text is, then write it. */
    va_copy(again, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) {
        va_end(again);
        return ERR_INVALID;
    }
    int err = buf_reserve(b, (size_t)n);
    if (err != 0) {
        va_end(again);
        return err;
    }
    int written = vsnprintf(b->data + b->len, (size_t)n + 1, fmt, again);
    va_end(again);
    if (written != n) {
        b->data[b->len] = '\0';
        return ERR_INVALID;
    }
    b->len += (size_t)n;
    return 0;
}

int
buf_appendf(struct buf *b, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    int err = buf_vappendf(b, fmt, ap);
    va_end(ap);
    return err;
}

char *
buf_tail(struct buf *b)
{
    /* Even adding 0 to a null pointer is undefined in C. */
    return b->data != NULL ? b->data + b->len : NULL;
}

void
buf_commit(struct buf *b, size_t written)
{
    /* Never past the room reserved, whatever the caller says. */
    size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
    if (written > room) {
        written = room;
    }
    b->len += written;
    if (b->data != NULL) {
        b->data[b->len] = '\0';
    }
}

void
buf_truncate(struct buf *b, size_t len)
{
    if (len < b->len) {
        b->len = len;
        b->data[len] = '\0';
    }
}

void
buf_consume(struct buf *b, size_t n)
{
    if (n >= b->len) {
        buf_clear(b);
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = '\0';
}

struct str
buf_str(const struct buf *b)
{
    return str_make(b->data != NULL ? b->data : "", b->len);
}

const char *
buf_cstr(const struct buf *b)
{
    return b->data != NULL ? b->data : "";
}
