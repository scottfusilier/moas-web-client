/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * SHA-256 (FIPS 180-4), for telling whether a settings file has changed and
 * for the page's Content-Security-Policy hashes.  Not used for anything
 * secret.
 */

#ifndef MOAS_CORE_SHA256_H
#define MOAS_CORE_SHA256_H

#include "core/common.h"

#define SHA256_SIZE 32

/* Text forms, including the terminating NUL. */
#define SHA256_HEX_SIZE (SHA256_SIZE * 2 + 1)
#define SHA256_BASE64_SIZE 45

void sha256(const void *data, size_t len, uint8_t out[SHA256_SIZE]);

/* The digest as lower-case hexadecimal. */
void sha256_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]);

/* The digest in base64, as Content-Security-Policy hashes are written. */
void sha256_base64(const void *data, size_t len, char out[SHA256_BASE64_SIZE]);

#endif /* MOAS_CORE_SHA256_H */
