/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* One message, decoded as if the framer had found it. */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "fuzz.h"
#include "oasp/oasp.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct oasp_message msg;

    if (oasp_decode(str_make((const char *)data, size), &msg) != 0) {
        return 0;
    }
    /* Decoded text is always clean and terminated within its field. */
    if (msg.kind == OASP_SERVER &&
        strlen(msg.u.server.name) >= sizeof(msg.u.server.name))
    {
        abort();
    }
    oasp_message_free(&msg);
    return 0;
}
