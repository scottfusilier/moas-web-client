/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A request head, as if from a browser (or anything that can reach the
 * page), and the guard's checks on it.
 */

#include "core/common.h"

#include <stdlib.h>

#include "fuzz.h"
#include "http/http.h"
#include "http/parse.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct http_request req;
    size_t head_len = 0;
    int status = 0;
    struct str in = str_make((const char *)data, size);

    int err = http_parse_head(in, &req, &head_len, &status);
    if (err == ERR_AGAIN) {
        return 0;
    }
    if (err != 0) {
        /* A refusal always says how to answer. */
        if (status < 400 || status > 599) {
            abort();
        }
        return 0;
    }
    if (head_len == 0 || head_len > size || req.path.len == 0 ||
        req.path.ptr[0] != '/' || req.content_length > CAPS_HTTP_BODY)
    {
        abort();
    }
    const char *message = NULL;
    for (int i = 0; i < 4; i++) {
        req.peer_is_loopback = (i & 1) != 0;
        int refused = http_guard(&req, (i & 2) != 0, true, NULL, &message);
        if (refused != 0 && message == NULL) {
            abort();
        }
    }
    return 0;
}
