# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier

# moas_test(<name> [LIBS <library>...] [SOURCES <file>...]) builds
# test_<name>.c, plus any extra sources, into a test program, links it
# against the given libraries and registers it with CTest.  Tests run in the
# build folder, where they may create files.
function(moas_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBS;SOURCES")
    add_executable(test_${name} test_${name}.c ${arg_SOURCES})
    moas_warnings(test_${name})
    target_link_libraries(test_${name} PRIVATE ${arg_LIBS})
    add_test(NAME ${name} COMMAND test_${name}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

moas_test(third_party LIBS moas_cjson moas_picohttpparser)

# Platform layer.
moas_test(net LIBS moas_platform)
moas_test(fs LIBS moas_platform)
moas_test(signal LIBS moas_core)

# Core layer.
moas_test(str LIBS moas_core)
moas_test(buf LIBS moas_core)
moas_test(json LIBS moas_core)
moas_test(log LIBS moas_core)
moas_test(loop LIBS moas_core)

# The MOAS protocol.
moas_test(frame LIBS moas_oasp)
moas_test(message LIBS moas_oasp)
moas_test(client LIBS moas_oasp SOURCES fake_moas_server.c)

# Frequency sources.
moas_test(tracker LIBS moas_freq)
moas_test(n1mm LIBS moas_freq)
moas_test(rigctld LIBS moas_freq)

# The HTTP server.
moas_test(http_parse LIBS moas_http)
moas_test(http_server LIBS moas_http)

# moasweb.
moas_test(args LIBS moas_app)
moas_test(watch LIBS moas_app SOURCES fake_moas_server.c)

add_test(NAME banned_functions
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P "${PROJECT_SOURCE_DIR}/cmake/check_banned.cmake")
