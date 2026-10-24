# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# "format" rewrites our source files to the project style; "format-check"
# only reports differences.  Both need clang-format; the reference is version 23.
# Third-party code is left alone.

find_program(CLANG_FORMAT clang-format)
if(NOT CLANG_FORMAT)
    return()
endif()

file(GLOB_RECURSE format_files CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/tests/*.c"
    "${CMAKE_SOURCE_DIR}/tests/*.h"
    "${CMAKE_SOURCE_DIR}/fuzz/*.c"
    "${CMAKE_SOURCE_DIR}/fuzz/*.h")

add_custom_target(format
    COMMAND "${CLANG_FORMAT}" -i ${format_files}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting the source files"
    VERBATIM)

add_custom_target(format-check
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${format_files}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking the formatting of the source files"
    VERBATIM)
