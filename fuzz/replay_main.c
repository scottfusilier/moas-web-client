/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Where libFuzzer is not available (MinGW on Windows), each fuzz target is
 * built with this main() instead: it feeds the target every file named on
 * the command line, the stored corpus, once.  That keeps the targets
 * building and passing on every system, between real fuzzing runs.
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>

#include "fuzz.h"
#include "platform/fs.h"
#include "platform/proc.h"

#define MAX_INPUT (4 * 1024 * 1024)

int
main(int argc, char **argv)
{
    if (plat_process_init(&argc, &argv) != 0) {
        return 1;
    }
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        char *data = NULL;
        size_t len = 0;
        int err = plat_read_file(argv[i], MAX_INPUT, &data, &len);
        if (err != 0) {
            fprintf(stderr, "%s: cannot read: %s\n", argv[i], err_str(err));
            failed = 1;
            continue;
        }
        LLVMFuzzerTestOneInput((const uint8_t *)data, len);
        free(data);
    }
    printf("%d input%s replayed\n", argc - 1, argc == 2 ? "" : "s");
    return failed;
}
