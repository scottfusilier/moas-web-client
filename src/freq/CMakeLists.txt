# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Frequency sources and the tracker that chooses between them.  Uses only the
# core and platform layers.

add_library(moas_freq STATIC
    n1mm.c
    n1mm_xml.c
    n3fjp.c
    n3fjp_reply.c
    registry.c
    rigctld.c
    rigctld_reply.c
    tracker.c)
moas_warnings(moas_freq)
target_link_libraries(moas_freq PUBLIC moas_core)
