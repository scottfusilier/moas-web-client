# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# moas_warnings(<target>) turns on the project's compiler warnings for one of
# our own targets.  Third-party code is not built with these.  MOAS_WERROR
# makes them errors; the presets turn it on.

function(moas_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        if(MOAS_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wold-style-definition
            -Wvla
            -Wcast-qual
            -Wpointer-arith
            -Wundef
            -Wwrite-strings
            -Wimplicit-fallthrough)
        if(MOAS_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
