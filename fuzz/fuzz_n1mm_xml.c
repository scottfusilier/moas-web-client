/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* One UDP packet, as if from N1MM (or anything else on the network). */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "freq/n1mm_xml.h"
#include "fuzz.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct n1mm_radio_info info;

    if (n1mm_parse_radio_info(str_make((const char *)data, size), &info) != 0)
    {
        return 0;
    }
    /* The station name always ends within its field. */
    if (strlen(info.station_name) >= sizeof(info.station_name) ||
        n1mm_khz(&info) < 0)
    {
        abort();
    }
    return 0;
}
