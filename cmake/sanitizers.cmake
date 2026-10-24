# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# MOAS_SANITIZE builds everything, third-party code included, with
# AddressSanitizer, and with UndefinedBehaviorSanitizer where the compiler
# has it.  Any problem found stops the program.
#
# MOAS_FUZZ also instruments everything for libFuzzer, so that the fuzzer
# can see which code each input reaches; the fuzz targets themselves link
# libFuzzer (fuzz/CMakeLists.txt).

if(MOAS_FUZZ)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MOAS_FUZZ needs clang (libFuzzer); this is "
                            "${CMAKE_C_COMPILER_ID}.  Set CC=clang.")
    endif()
    set(MOAS_SANITIZE ON)
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

if(NOT MOAS_SANITIZE)
    return()
endif()

if(MSVC)
    add_compile_options(/fsanitize=address /Zi)
else()
    set(sanitize -fsanitize=address,undefined)
    add_compile_options(${sanitize}
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer)
    add_link_options(${sanitize})
endif()
