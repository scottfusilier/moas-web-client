# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Hardening for every target, third-party code included.  Each flag is used
# only if the compiler and linker accept it without even a warning: some
# compilers accept a flag they cannot honour on this system and only warn,
# which MOAS_WERROR would turn into a failed build.

include(CheckCCompilerFlag)
include(CheckLinkerFlag)
include(CheckPIESupported)

check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/GS /sdl /guard:cf)
    add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_link_options(/HIGHENTROPYVA /CETCOMPAT)
    endif()
    return()
endif()

set(CMAKE_REQUIRED_FLAGS -Werror)

foreach(flag
        -fstack-protector-strong
        -fstack-clash-protection
        -fcf-protection=full)
    string(MAKE_C_IDENTIFIER "have${flag}" var)
    check_c_compiler_flag(${flag} ${var})
    if(${var})
        add_compile_options(${flag})
    endif()
endforeach()

# _FORTIFY_SOURCE needs optimisation, so it is left out of Debug builds.
# Some systems define it already; replace their value with ours.  It is also
# left out of sanitizer builds, which check the same things more thoroughly
# and do not work well alongside it.
if(NOT WIN32 AND NOT MOAS_SANITIZE AND NOT MOAS_FUZZ)
    add_compile_options(
        "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>")
endif()

if(WIN32)
    set(link_flags
        -Wl,--dynamicbase
        -Wl,--nxcompat
        -Wl,--high-entropy-va)
elseif(NOT APPLE)
    set(link_flags
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack)
endif()
foreach(flag IN LISTS link_flags)
    string(MAKE_C_IDENTIFIER "have${flag}" var)
    check_linker_flag(C ${flag} ${var})
    if(${var})
        add_link_options(${flag})
    endif()
endforeach()

unset(CMAKE_REQUIRED_FLAGS)
