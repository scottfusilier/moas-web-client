/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A growable byte buffer with a hard maximum size.  Every write is checked:
 * one that would take the contents past the maximum fails with ERR_LIMIT
 * and leaves the buffer as it was.  The contents are always followed by a
 * NUL byte (not counted in len), so text in a buffer can be passed to
 * functions that need a C string.
 *
 * A buffer is usually part of another struct, so it is set up with
 * buf_init() and released with buf_fini() rather than created and
 * destroyed.  It allocates nothing until the first write.
 */

#ifndef MOAS_CORE_BUF_H
#define MOAS_CORE_BUF_H

#include <stdarg.h>

#include "core/common.h"
#include "core/str.h"

struct buf {
    char *data; /* NULL until the first write */
    size_t len; /* bytes of contents */
    size_t cap; /* bytes allocated, including room for the NUL */
    size_t max; /* the most len may ever be */
};

void buf_init(struct buf *b, size_t max);

/* Frees the memory; the buffer is then empty, as after buf_init(). */
void buf_fini(struct buf *b);

/* Empties the buffer, keeping its memory for reuse. */
void buf_clear(struct buf *b);

/* Makes room for extra more bytes.  ERR_LIMIT past max, or ERR_NOMEM. */
int buf_reserve(struct buf *b, size_t extra);

int buf_append(struct buf *b, const void *data, size_t len);
int buf_append_str(struct buf *b, struct str s);
int buf_append_cstr(struct buf *b, const char *s);

/* Appends printf-style formatted text. */
int buf_appendf(struct buf *b, const char *fmt, ...) MOAS_PRINTF(2, 3);
int buf_vappendf(struct buf *b, const char *fmt, va_list ap) MOAS_PRINTF(2, 0);

/*
 * For reading straight into the buffer: after buf_reserve(b, n) succeeds,
 * up to n bytes may be written at buf_tail(b), and buf_commit(b, written)
 * then adds them to the contents.
 */
char *buf_tail(struct buf *b);
void buf_commit(struct buf *b, size_t written);

/* Shortens the contents to len bytes; does nothing if they are shorter. */
void buf_truncate(struct buf *b, size_t len);

/* Removes n bytes from the front (all of them if n >= len). */
void buf_consume(struct buf *b, size_t n);

/* The contents as a slice, valid until the buffer next changes. */
struct str buf_str(const struct buf *b);

/* The contents as a C string ("" when empty), valid until the next change. */
const char *buf_cstr(const struct buf *b);

#endif /* MOAS_CORE_BUF_H */
