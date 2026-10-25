# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Fuzz targets, one per parser of outside input.  With MOAS_FUZZ (the
# "fuzz" preset; clang only) each is a libFuzzer program, run by
# scripts/fuzz.sh.  Otherwise each replays its stored corpus as a test.

# moas_fuzz(<name> LIBS <library>...) builds fuzz_<name>.c.
function(moas_fuzz name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBS")
    if(MOAS_FUZZ)
        add_executable(fuzz_${name} fuzz_${name}.c)
        target_compile_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    else()
        add_executable(fuzz_${name} fuzz_${name}.c replay_main.c)
        file(GLOB corpus CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}/*")
        add_test(NAME fuzz_${name}_replay COMMAND fuzz_${name} ${corpus})
    endif()
    moas_warnings(fuzz_${name})
    target_link_libraries(fuzz_${name} PRIVATE ${arg_LIBS})
endfunction()

moas_fuzz(frame LIBS moas_oasp)
moas_fuzz(message LIBS moas_oasp)
moas_fuzz(n1mm_xml LIBS moas_freq)
moas_fuzz(rigctld_reply LIBS moas_freq)
moas_fuzz(http_parse LIBS moas_http)
moas_fuzz(settings LIBS moas_app)
