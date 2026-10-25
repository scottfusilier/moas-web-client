# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Turns a file into a C array, so that it is built into the program:
#
#   cmake -DINPUT=page.html -DOUTPUT=page_data.c -DNAME=app_page_html
#         -DHEADER=app/page.h -P embed.cmake
#
# The array is NAME (the bytes, then a NUL), and NAME_len its length
# without the NUL, as declared in HEADER.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR length "${digits} / 2")
# Sixteen bytes to a line.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,)(0x..,))"
       "\\1\n    " bytes "${bytes}")
file(WRITE "${OUTPUT}"
"/* Generated from ${INPUT} by cmake/embed.cmake; do not edit. */

#include \"${HEADER}\"

const unsigned char ${NAME}[] = {
    ${bytes}0x00
};
const size_t ${NAME}_len = ${length};
")
