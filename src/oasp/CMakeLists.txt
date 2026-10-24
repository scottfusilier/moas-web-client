# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# The MOAS protocol library.  Uses only the core and platform layers, so it
# can be used on its own, for example by a future server.

add_library(moas_oasp STATIC
    client.c
    frame.c
    message.c)
moas_warnings(moas_oasp)
target_link_libraries(moas_oasp PUBLIC moas_core)
