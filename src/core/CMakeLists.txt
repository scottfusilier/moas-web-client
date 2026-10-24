# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# The core layer: buffers, slices, JSON, logging and the event loop.  Uses
# only the platform layer and cJSON.

add_library(moas_core STATIC
    buf.c
    json.c
    log.c
    loop.c
    str.c)
moas_warnings(moas_core)
target_link_libraries(moas_core PUBLIC moas_platform moas_cjson)
