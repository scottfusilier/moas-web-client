/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A small test harness.  Each test program is one .c file whose main() calls
 * RUN() for each test function and returns test_summary().  A failed CHECK
 * reports the file, line and expression and the test carries on; the program
 * then exits with status 1, which CTest counts as a failure.
 *
 * Include this from test programs only, and only once per program.
 */

#ifndef MOAS_TEST_H
#define MOAS_TEST_H

#include <stdio.h>
#include <string.h>

static int test_checks;
static int test_failures;

static inline void
test_check(int ok, const char *expr, const char *file, int line)
{
    test_checks++;
    if (!ok) {
        test_failures++;
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
    }
}

static inline void
test_check_int(long long got, long long want, const char *got_expr,
               const char *want_expr, const char *file, int line)
{
    test_checks++;
    if (got != want) {
        test_failures++;
        fprintf(stderr, "%s:%d: %s == %s failed: got %lld, want %lld\n", file,
                line, got_expr, want_expr, got, want);
    }
}

static inline void
test_check_mem(const void *got, size_t got_len, const void *want,
               size_t want_len, const char *got_expr, const char *file,
               int line)
{
    test_checks++;
    /* memcmp() must not be given a null pointer, even with length 0. */
    int same = got_len == want_len &&
               (got_len == 0 || memcmp(got, want, got_len) == 0);
    if (!same) {
        test_failures++;
        fprintf(stderr, "%s:%d: %s: got \"%.*s\", want \"%.*s\"\n", file, line,
                got_expr, (int)got_len, (const char *)got, (int)want_len,
                (const char *)want);
    }
}

static inline void
test_run(const char *name, void (*fn)(void))
{
    int before = test_failures;

    fn();
    printf("%s %s\n", test_failures == before ? "ok  " : "FAIL", name);
}

static inline int
test_summary(void)
{
    printf("%d checks, %d failed\n", test_checks, test_failures);
    return test_failures == 0 ? 0 : 1;
}

/* True if cond holds. */
#define CHECK(cond) test_check((cond) ? 1 : 0, #cond, __FILE__, __LINE__)

/* Two integer values are equal. */
#define CHECK_INT_EQ(got, want)                                               \
    test_check_int((long long)(got), (long long)(want), #got, #want,          \
                   __FILE__, __LINE__)

/* A (pointer, length) string equals a C string literal or array. */
#define CHECK_MEM_EQ(got, got_len, want)                                      \
    test_check_mem((got), (got_len), (want), strlen(want), #got, __FILE__,    \
                   __LINE__)

#define RUN(fn) test_run(#fn, fn)

#endif /* MOAS_TEST_H */
