/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* One reply line, as if from rigctld. */

#include "core/common.h"

#include <stdlib.h>

#include "freq/rigctld_reply.h"
#include "fuzz.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int khz = 0;
    int report = 0;

    if (rigctld_parse_reply(str_make((const char *)data, size), &khz,
                            &report) == 0 &&
        khz < 1)
    {
        abort(); /* a reading is always at least 1 kHz */
    }
    return 0;
}
