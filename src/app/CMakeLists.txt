# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# moasweb itself.  Everything but main() is a library, so that tests can
# use it.

add_library(moas_app STATIC
    args.c
    watch.c)
moas_warnings(moas_app)
target_link_libraries(moas_app PUBLIC moas_oasp)

add_executable(moasweb main.c)
moas_warnings(moasweb)
target_link_libraries(moasweb PRIVATE moas_app)
target_compile_definitions(moasweb PRIVATE
    MOAS_VERSION="${PROJECT_VERSION}")
