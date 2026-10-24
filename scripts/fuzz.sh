#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Scott Fusilier
#
# Builds the fuzz targets with clang and libFuzzer, and runs each one for a
# while.  For FreeBSD and Linux; libFuzzer is not available with MinGW.
#
#   scripts/fuzz.sh            each target for 60 seconds
#   scripts/fuzz.sh 600        each target for 10 minutes
#
# New interesting inputs are kept in build/fuzz/corpus/<target>/, so later
# runs carry on where earlier ones stopped.  Anything that crashes is saved
# in build/fuzz/crashes/; the target then stops and this script fails.
# Copy a crashing input into fuzz/corpus/<target>/ once it is fixed, so the
# replay tests keep checking it.
#
# Needs clang with libFuzzer: part of the base system on FreeBSD; on Debian
# and Ubuntu, apt install clang (and libclang-rt-dev if linking fails).

set -e
cd "$(dirname "$0")/.."

seconds=${1:-60}
CC=${CC:-clang}
export CC

cmake --preset fuzz
cmake --build --preset fuzz

mkdir -p build/fuzz/crashes
status=0
for target in build/fuzz/fuzz/fuzz_*; do
    [ -x "$target" ] || continue
    name=$(basename "$target")
    name=${name#fuzz_}
    corpus=build/fuzz/corpus/$name
    mkdir -p "$corpus"
    echo "==== $name for $seconds seconds"
    if ! "$target" -max_total_time="$seconds" -print_final_stats=1 \
        -artifact_prefix=build/fuzz/crashes/"$name"- \
        "$corpus" "fuzz/corpus/$name"; then
        echo "==== $name FAILED; see build/fuzz/crashes/" >&2
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "==== no problems found"
fi
exit "$status"
