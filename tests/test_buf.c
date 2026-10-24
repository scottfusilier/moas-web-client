/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "core/buf.h"
#include "test.h"

static void
test_empty(void)
{
    struct buf b;
    buf_init(&b, 100);

    CHECK(b.data == NULL);
    CHECK_INT_EQ(b.len, 0);
    CHECK_MEM_EQ(buf_cstr(&b), strlen(buf_cstr(&b)), "");
    struct str s = buf_str(&b);
    CHECK_INT_EQ(s.len, 0);
    buf_consume(&b, 5);
    buf_commit(&b, 5); /* nothing reserved: adds nothing */
    CHECK_INT_EQ(b.len, 0);
    buf_fini(&b);
}

static void
test_append_and_nul(void)
{
    struct buf b;
    buf_init(&b, 100);

    CHECK_INT_EQ(buf_append_cstr(&b, "Station"), 0);
    CHECK_INT_EQ(buf_append_str(&b, STR_LIT(" 1")), 0);
    CHECK_INT_EQ(buf_appendf(&b, ", radio %d", 2), 0);
    CHECK_MEM_EQ(b.data, b.len, "Station 1, radio 2");
    CHECK(b.data[b.len] == '\0');
    CHECK_MEM_EQ(buf_cstr(&b), strlen(buf_cstr(&b)), "Station 1, radio 2");

    buf_clear(&b);
    CHECK_INT_EQ(b.len, 0);
    CHECK(b.data[0] == '\0');
    buf_fini(&b);
}

static void
test_limit_is_exact(void)
{
    struct buf b;
    buf_init(&b, 10);

    CHECK_INT_EQ(buf_append_cstr(&b, "0123456789"), 0); /* exactly max */
    CHECK_INT_EQ(b.len, 10);
    /* One byte more fails, by any route, and changes nothing. */
    CHECK_INT_EQ(buf_append_cstr(&b, "x"), ERR_LIMIT);
    CHECK_INT_EQ(buf_appendf(&b, "%d", 1), ERR_LIMIT);
    CHECK_INT_EQ(buf_reserve(&b, 1), ERR_LIMIT);
    CHECK_INT_EQ(buf_reserve(&b, SIZE_MAX), ERR_LIMIT);
    CHECK_MEM_EQ(b.data, b.len, "0123456789");
    CHECK(b.cap <= 11); /* never allocates past max + NUL */
    buf_fini(&b);

    /* A huge max does not let len + 1 wrap around. */
    buf_init(&b, SIZE_MAX);
    CHECK_INT_EQ(buf_append_cstr(&b, "ok"), 0);
    CHECK_INT_EQ(buf_reserve(&b, SIZE_MAX), ERR_LIMIT);
    buf_fini(&b);
}

static void
test_growth_keeps_contents(void)
{
    struct buf b;
    buf_init(&b, 100000);

    for (int i = 0; i < 1000; i++) {
        CHECK_INT_EQ(buf_appendf(&b, "%03d,", i % 1000), 0);
    }
    CHECK_INT_EQ(b.len, 4000);
    CHECK_MEM_EQ(b.data, 8, "000,001,");
    CHECK_MEM_EQ(b.data + 3996, 4, "999,");
    buf_fini(&b);
}

static void
test_reserve_commit_consume(void)
{
    struct buf b;
    buf_init(&b, 64);

    /* As when reading from a socket. */
    CHECK_INT_EQ(buf_reserve(&b, 16), 0);
    memcpy(buf_tail(&b), "{\"a\":1}{\"b\"", 11);
    buf_commit(&b, 11);
    CHECK_MEM_EQ(b.data, b.len, "{\"a\":1}{\"b\"");

    /* The first object is handled; the rest stays. */
    buf_consume(&b, 7);
    CHECK_MEM_EQ(b.data, b.len, "{\"b\"");
    CHECK(b.data[b.len] == '\0');

    /* Committing more than was reserved is clamped. */
    CHECK_INT_EQ(buf_reserve(&b, 2), 0);
    size_t room = b.cap - b.len - 1;
    buf_commit(&b, room + 1000);
    CHECK(b.len < b.cap);
    CHECK(b.data[b.len] == '\0');

    buf_consume(&b, SIZE_MAX);
    CHECK_INT_EQ(b.len, 0);
    buf_fini(&b);
}

int
main(void)
{
    RUN(test_empty);
    RUN(test_append_and_nul);
    RUN(test_limit_is_exact);
    RUN(test_growth_keeps_contents);
    RUN(test_reserve_commit_consume);
    return test_summary();
}
