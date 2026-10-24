/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The server's byte stream, as the client handles it: arriving in pieces,
 * split into messages by the framer, and each message decoded.  The first
 * byte of the input sets the size of the pieces.
 */

#include "core/common.h"

#include "core/buf.h"
#include "core/caps.h"
#include "fuzz.h"
#include "oasp/frame.h"
#include "oasp/oasp.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return 0;
    }
    size_t piece = (size_t)(data[0] % 64) + 1;
    const char *stream = (const char *)data + 1;
    size_t len = size - 1;

    struct buf b;
    struct oasp_framer f;
    buf_init(&b, CAPS_OASP_MESSAGE + 4096);
    oasp_framer_reset(&f);

    size_t fed = 0;
    while (fed < len) {
        size_t n = len - fed < piece ? len - fed : piece;
        if (buf_append(&b, stream + fed, n) != 0) {
            break;
        }
        fed += n;

        int err = 0;
        while (err == 0) {
            size_t start;
            size_t end;
            err = oasp_frame_next(&f, b.data, b.len, &start, &end);
            if (err != 0) {
                break;
            }
            struct oasp_message msg;
            if (oasp_decode(str_make(b.data + start, end - start), &msg) == 0)
            {
                oasp_message_free(&msg);
            }
            buf_consume(&b, end);
            oasp_framer_reset(&f);
        }
        if (err != ERR_AGAIN) {
            break; /* the client would drop the connection here */
        }
    }
    buf_fini(&b);
    return 0;
}
