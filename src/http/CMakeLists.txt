# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# The HTTP server for the page.  Uses only the core and platform layers, and
# picohttpparser (inside parse.c only).

add_library(moas_http STATIC
    guard.c
    parse.c
    router.c
    server.c)
moas_warnings(moas_http)
target_link_libraries(moas_http PUBLIC moas_core PRIVATE moas_picohttpparser)
