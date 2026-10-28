/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* One line, as if from an N3FJP program's TCP API. */

#include "core/common.h"

#include <stdlib.h>

#include "freq/n3fjp_reply.h"
#include "fuzz.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int khz = 0;

    if (n3fjp_parse_line(str_make((const char *)data, size), &khz) == 0 &&
        khz < 1)
    {
        abort(); /* a reading is always at least 1 kHz */
    }
    return 0;
}
