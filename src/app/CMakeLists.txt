# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# moasweb itself.  Everything but main() is a library, so that tests can
# use it.

# page.html, built into the program as a C array (cmake/embed.cmake).
set(page_data "${CMAKE_CURRENT_BINARY_DIR}/page_data.c")
add_custom_command(
    OUTPUT "${page_data}"
    COMMAND "${CMAKE_COMMAND}"
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/page.html
            -DOUTPUT=${page_data}
            -DNAME=app_page_html
            -DHEADER=app/page.h
            -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS page.html "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "Building page.html into the program"
    VERBATIM)

add_library(moas_app STATIC
    args.c
    link.c
    runner.c
    serve.c
    settings.c
    settings_file.c
    watch.c
    web.c
    "${page_data}")
moas_warnings(moas_app)
target_link_libraries(moas_app PUBLIC moas_oasp moas_freq moas_http)

add_executable(moasweb main.c)
moas_warnings(moasweb)
target_link_libraries(moasweb PRIVATE moas_app)
target_compile_definitions(moasweb PRIVATE
    MOAS_VERSION="${PROJECT_VERSION}")
