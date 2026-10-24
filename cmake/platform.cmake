# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Which platform layer to build.  src/platform/ has a _posix.c and a _win.c
# file for each part; MOAS_PLATFORM picks one of each.

if(WIN32)
    set(MOAS_PLATFORM win)
    # Windows 10 or later.
    add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    set(MOAS_PLATFORM posix)
endif()
