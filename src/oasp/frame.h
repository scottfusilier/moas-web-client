/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Splitting the server's byte stream into messages.  The server sends JSON
 * objects back to back, and TCP may split or join them anywhere, so the
 * framer finds where each object ends by following braces and brackets,
 * ignoring those inside strings.  It does not check that the object is
 * valid JSON; oasp_decode() does that.
 *
 * Use: append received bytes to a buffer, then call oasp_frame_next() on
 * the buffer until it returns ERR_AGAIN.  After each object, remove the
 * bytes up to *end from the front of the buffer and call
 * oasp_framer_reset().  The framer remembers how far it has looked, so
 * bytes are only examined once.
 */

#ifndef MOAS_OASP_FRAME_H
#define MOAS_OASP_FRAME_H

#include "core/common.h"

struct oasp_framer {
    size_t pos;   /* bytes of the buffer already examined */
    size_t start; /* where the current object began */
    size_t depth; /* open braces and brackets */
    bool in_string;
    bool escape; /* the previous character was a backslash in a string */
};

void oasp_framer_reset(struct oasp_framer *f);

/*
 * Looks for the end of the first object in data[0..len).  Returns 0 with
 * data[*start..*end) the object; ERR_AGAIN if it is not complete yet;
 * ERR_INVALID if something other than whitespace comes before an object,
 * or the brackets do not match up; ERR_LIMIT if the object is longer than
 * CAPS_OASP_MESSAGE or nested deeper than CAPS_JSON_DEPTH.  After an error
 * the stream cannot be trusted and the connection should be dropped.
 */
int oasp_frame_next(struct oasp_framer *f, const char *data, size_t len,
                    size_t *start, size_t *end);

#endif /* MOAS_OASP_FRAME_H */
