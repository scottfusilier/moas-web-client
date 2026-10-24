# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Fails if any of our source files calls a banned C library function.  Run
# as a test: cmake -DSOURCE_DIR=<dir> -P check_banned.cmake
#
# CONTRIBUTING.md lists what to use instead.

# Banned everywhere: they cannot be told how big the destination is, or are
# easy to misuse.
set(banned
    strcpy strncpy strcat strncat
    sprintf vsprintf
    gets
    strtok
    atoi atol atoll atof
    tmpnam mktemp)

# Allowed only in src/platform/: on Windows they take paths and names in the
# local code page rather than UTF-8, so the rest of the program uses the
# platform layer's functions instead.
set(platform_only
    fopen freopen remove rename unlink mkdir rmdir
    getenv)

# A call: the name, not preceded by a letter, digit or underscore, followed by
# optional spaces and an opening parenthesis.
function(call_regex out names)
    list(JOIN names "|" alt)
    set(${out} "(^|[^A-Za-z0-9_])(${alt})[ \t]*\\(" PARENT_SCOPE)
endfunction()
call_regex(banned_re "${banned}")
call_regex(platform_re "${platform_only}")

file(GLOB_RECURSE files
    "${SOURCE_DIR}/src/*.c" "${SOURCE_DIR}/src/*.h"
    "${SOURCE_DIR}/tests/*.c" "${SOURCE_DIR}/tests/*.h"
    "${SOURCE_DIR}/fuzz/*.c" "${SOURCE_DIR}/fuzz/*.h")

set(found 0)
foreach(file IN LISTS files)
    file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
    set(in_platform 0)
    if(rel MATCHES "^src/platform/")
        set(in_platform 1)
    endif()
    file(STRINGS "${file}" lines)
    set(number 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        if(line MATCHES "${banned_re}")
            message("${rel}:${number}: banned function ${CMAKE_MATCH_2}()")
            set(found 1)
        elseif(NOT in_platform AND line MATCHES "${platform_re}")
            message("${rel}:${number}: ${CMAKE_MATCH_2}() is for "
                    "src/platform/ only; use the platform layer")
            set(found 1)
        endif()
    endforeach()
endforeach()

if(found)
    message(FATAL_ERROR "Banned functions are used; see CONTRIBUTING.md.")
endif()
message("No banned functions in ${SOURCE_DIR}.")
