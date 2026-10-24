# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# The vendored libraries, built as they come from upstream.  Their headers are
# SYSTEM includes, so our stricter warnings do not apply to them.  See
# README.md here for versions and reasons.

add_library(moas_cjson STATIC cjson/cJSON.c)
target_include_directories(moas_cjson SYSTEM PUBLIC cjson)
target_compile_definitions(moas_cjson PUBLIC
    # A static library: export nothing from the program.
    CJSON_HIDE_SYMBOLS
    # Must equal CAPS_JSON_DEPTH in src/core/caps.h; core/json.c checks.
    CJSON_NESTING_LIMIT=32)
# cJSON uses the C maths library on Unix.
if(NOT WIN32)
    target_link_libraries(moas_cjson PUBLIC m)
endif()

add_library(moas_picohttpparser STATIC picohttpparser/picohttpparser.c)
target_include_directories(moas_picohttpparser SYSTEM PUBLIC picohttpparser)
