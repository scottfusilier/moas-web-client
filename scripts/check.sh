#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Builds and tests every preset (debug, asan, release) and checks the
# formatting.  A stage or change is done when this passes on Windows,
# FreeBSD and Linux.
#
#   scripts/check.sh
#
# Needs cmake, ninja and a C compiler (cc, or set CC); clang-format for the
# formatting check.  On Windows, scripts\check.bat does the same.

set -e
cd "$(dirname "$0")/.."

for preset in debug asan release; do
    echo "==== $preset"
    cmake --preset "$preset"
    cmake --build --preset "$preset"
    ctest --preset "$preset"
done

echo "==== formatting"
if command -v clang-format >/dev/null 2>&1; then
    clang-format --version
    cmake --build --preset debug --target format-check
    echo "Formatting is clean."
else
    echo "clang-format not found; formatting not checked." >&2
    echo "The project's formatting is defined by clang-format 23." >&2
fi

echo "==== all checks passed"
