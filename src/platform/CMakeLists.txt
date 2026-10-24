# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# The platform layer: the only code that differs between systems.  For each
# part there is a _posix.c and a _win.c file; MOAS_PLATFORM (from
# cmake/platform.cmake) picks one of each.

add_library(moas_platform STATIC
    err.c
    net_addr.c
    net_pair.c
    clock_${MOAS_PLATFORM}.c
    fs_${MOAS_PLATFORM}.c
    net_${MOAS_PLATFORM}.c
    proc_${MOAS_PLATFORM}.c
    signal_${MOAS_PLATFORM}.c)
moas_warnings(moas_platform)

# Headers are included as "platform/net.h", "core/buf.h" and so on.
target_include_directories(moas_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")

if(WIN32)
    target_link_libraries(moas_platform PUBLIC ws2_32
        PRIVATE shell32 ole32 uuid)
else()
    # The strict C11 build hides POSIX interfaces unless asked for them.  On
    # macOS, _POSIX_C_SOURCE in turn hides some of Apple's own declarations
    # that its headers need; _DARWIN_C_SOURCE brings them back.
    target_compile_definitions(moas_platform PRIVATE _POSIX_C_SOURCE=200809L)
    if(APPLE)
        target_compile_definitions(moas_platform PRIVATE _DARWIN_C_SOURCE)
    endif()
endif()
