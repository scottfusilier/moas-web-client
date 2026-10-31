# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier

cmake_minimum_required(VERSION 3.21...4.3)

project(moas-web-client
    VERSION 1.0.0
    DESCRIPTION "Cross-platform client for the MOAS II antenna switch server"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-configuration generators (Ninja, Makefiles) default to Debug.
get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MOAS_WERROR "Treat compiler warnings as errors" OFF)
option(MOAS_SANITIZE
    "Build with AddressSanitizer (and UndefinedBehaviorSanitizer on gcc and clang)"
    OFF)
option(MOAS_BUILD_TESTS "Build the tests" ON)
option(MOAS_FUZZ
    "Build the fuzz targets with libFuzzer (clang only; implies MOAS_SANITIZE)"
    OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(platform)
include(warnings)
include(hardening)
include(sanitizers)
include(strip)
include(format)

add_subdirectory(third_party)
add_subdirectory(src)

if(MOAS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(fuzz)
endif()

include(package)
