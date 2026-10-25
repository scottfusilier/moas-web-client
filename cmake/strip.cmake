# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Release builds leave out the symbol table, which nothing needs at run
# time (it nearly doubles the size of the program).  Debug and sanitizer
# builds keep it, for debuggers and stack traces.
#
# Used only if the linker accepts -s without even a warning: macOS's linker
# warns that it is obsolete, so there it is skipped.  MSVC keeps symbols in
# a separate .pdb file anyway.

if(MSVC)
    return()
endif()

include(CheckLinkerFlag)

set(CMAKE_REQUIRED_FLAGS -Werror)
check_linker_flag(C -s have_strip)
unset(CMAKE_REQUIRED_FLAGS)

if(have_strip)
    add_link_options("$<$<CONFIG:Release,MinSizeRel>:-s>")
endif()
