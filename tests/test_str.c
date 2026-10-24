/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include "core/checked.h"
#include "core/str.h"
#include "test.h"

static void
test_compare(void)
{
    CHECK(str_eq(STR_LIT("abc"), STR_LIT("abc")));
    CHECK(!str_eq(STR_LIT("abc"), STR_LIT("abd")));
    CHECK(!str_eq(STR_LIT("abc"), STR_LIT("ab")));
    CHECK(str_eq(STR_LIT(""), str_make(NULL, 0)));
    CHECK(str_eq_nocase(STR_LIT("Content-Length"), STR_LIT("content-LENGTH")));
    CHECK(!str_eq_nocase(STR_LIT("Host"), STR_LIT("Hosts")));
    CHECK(str_starts_with(STR_LIT("/api/state"), STR_LIT("/api/")));
    CHECK(!str_starts_with(STR_LIT("/ap"), STR_LIT("/api/")));
}

static void
test_trim_and_split(void)
{
    struct str t = str_trim(STR_LIT(" \t value \r\n"));
    CHECK_MEM_EQ(t.ptr, t.len, "value");
    t = str_trim(STR_LIT("   "));
    CHECK_INT_EQ(t.len, 0);

    struct str head;
    struct str tail;
    CHECK(str_split(STR_LIT("Host: 127.0.0.1:8080"), ':', &head, &tail));
    CHECK_MEM_EQ(head.ptr, head.len, "Host");
    CHECK_MEM_EQ(tail.ptr, tail.len, " 127.0.0.1:8080");
    CHECK(!str_split(STR_LIT("no separator"), ':', &head, &tail));
    CHECK(str_split(STR_LIT("end:"), ':', &head, &tail));
    CHECK_INT_EQ(tail.len, 0);
}

static void
test_to_i64(void)
{
    int64_t v = 0;

    CHECK_INT_EQ(str_to_i64(STR_LIT("14025"), 0, 100000, &v), 0);
    CHECK_INT_EQ(v, 14025);
    CHECK_INT_EQ(str_to_i64(STR_LIT("-7"), -10, 10, &v), 0);
    CHECK_INT_EQ(v, -7);
    CHECK_INT_EQ(
        str_to_i64(STR_LIT("9223372036854775807"), INT64_MIN, INT64_MAX, &v),
        0);
    CHECK(v == INT64_MAX);
    CHECK_INT_EQ(
        str_to_i64(STR_LIT("-9223372036854775808"), INT64_MIN, INT64_MAX, &v),
        0);
    CHECK(v == INT64_MIN);

    /* Malformed: nothing is stored. */
    v = 42;
    CHECK_INT_EQ(str_to_i64(STR_LIT(""), 0, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT("-"), -10, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT("+5"), 0, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT(" 5"), 0, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT("5 "), 0, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT("0x10"), 0, 100, &v), ERR_INVALID);
    CHECK_INT_EQ(str_to_i64(STR_LIT("1.5"), 0, 10, &v), ERR_INVALID);
    CHECK_INT_EQ(v, 42);

    /* Out of range, including values too big for any int64_t. */
    CHECK_INT_EQ(str_to_i64(STR_LIT("11"), 0, 10, &v), ERR_RANGE);
    CHECK_INT_EQ(str_to_i64(STR_LIT("-1"), 0, 10, &v), ERR_RANGE);
    CHECK_INT_EQ(
        str_to_i64(STR_LIT("9223372036854775808"), INT64_MIN, INT64_MAX, &v),
        ERR_RANGE);
    CHECK_INT_EQ(str_to_i64(STR_LIT("99999999999999999999999"), INT64_MIN,
                            INT64_MAX, &v),
                 ERR_RANGE);
    /* Too big, but not digits throughout: still malformed. */
    CHECK_INT_EQ(str_to_i64(STR_LIT("99999999999999999999999x"), INT64_MIN,
                            INT64_MAX, &v),
                 ERR_INVALID);
    CHECK_INT_EQ(v, 42);
}

static void
test_checked(void)
{
    size_t r = 7;

    CHECK(checked_add(2, 3, &r) && r == 5);
    CHECK(checked_add(SIZE_MAX - 1, 1, &r) && r == SIZE_MAX);
    r = 7;
    CHECK(!checked_add(SIZE_MAX, 1, &r));
    CHECK(r == 7);
    CHECK(checked_mul(0, SIZE_MAX, &r) && r == 0);
    CHECK(checked_mul(SIZE_MAX / 2, 2, &r));
    CHECK(!checked_mul(SIZE_MAX / 2 + 1, 2, &r));
}

int
main(void)
{
    RUN(test_compare);
    RUN(test_trim_and_split);
    RUN(test_to_i64);
    RUN(test_checked);
    return test_summary();
}
