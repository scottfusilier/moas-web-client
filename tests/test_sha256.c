/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* SHA-256 against the test vectors of FIPS 180-2 and NIST. */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/sha256.h"
#include "test.h"

static void
check_hex(const char *text, size_t len, const char *want, int line)
{
    char hex[SHA256_HEX_SIZE];
    sha256_hex(text, len, hex);
    test_check_mem(hex, strlen(hex), want, strlen(want), "sha256", __FILE__,
                   line);
}

static void
test_vectors(void)
{
    check_hex("", 0,
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852"
              "b855",
              __LINE__);
    check_hex("abc", 3,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f200"
              "15ad",
              __LINE__);
    const char two[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check_hex(two, sizeof(two) - 1,
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db"
              "06c1",
              __LINE__);
    /* 55, 56 and 64 bytes: where the padding needs a second block. */
    char a[200];
    memset(a, 'a', sizeof(a));
    check_hex(a, 55,
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f73"
              "4318",
              __LINE__);
    check_hex(a, 56,
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6"
              "738a",
              __LINE__);
    check_hex(a, 64,
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df1546"
              "68eb",
              __LINE__);

    char *million = malloc(1000000);
    CHECK(million != NULL);
    if (million != NULL) {
        memset(million, 'a', 1000000);
        check_hex(million, 1000000,
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7"
                  "112cd0",
                  __LINE__);
        free(million);
    }
}

static void
test_base64(void)
{
    char b64[SHA256_BASE64_SIZE];
    sha256_base64("abc", 3, b64);
    CHECK_MEM_EQ(b64, strlen(b64),
                 "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
    sha256_base64("", 0, b64);
    CHECK_MEM_EQ(b64, strlen(b64),
                 "47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=");
}

int
main(void)
{
    RUN(test_vectors);
    RUN(test_base64);
    return test_summary();
}
