/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Each fuzz target defines this function, which libFuzzer calls with
 * generated input.  It must not crash, leak or touch memory it does not
 * own, whatever the input; the sanitizers catch it if it does.
 */

#ifndef MOAS_FUZZ_FUZZ_H
#define MOAS_FUZZ_FUZZ_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#endif /* MOAS_FUZZ_FUZZ_H */
