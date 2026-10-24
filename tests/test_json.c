/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* core/json and the text cleaning it relies on. */

#include "core/common.h"

#include <string.h>

#include "core/caps.h"
#include "core/json.h"
#include "core/str.h"
#include "test.h"

static int
parse(const char *text, cJSON **out)
{
    return json_parse(str_from_cstr(text), out);
}

static void
test_parse(void)
{
    cJSON *j = NULL;

    CHECK_INT_EQ(parse(" {\"a\":1} \r\n", &j), 0);
    cJSON_Delete(j);
    CHECK_INT_EQ(parse("{\"a\":1} x", &j), ERR_INVALID);
    CHECK_INT_EQ(parse("{\"a\":1}{}", &j), ERR_INVALID);
    CHECK_INT_EQ(parse("", &j), ERR_INVALID);
    CHECK_INT_EQ(parse("{\"a\":}", &j), ERR_INVALID);

    /* CAPS_JSON_DEPTH levels parse; one more does not. */
    /* Up to CAPS_JSON_DEPTH + 1 levels: two brackets each, and the NUL. */
    char deep[(CAPS_JSON_DEPTH + 1) * 2 + 1];
    for (int depth = CAPS_JSON_DEPTH; depth <= CAPS_JSON_DEPTH + 1; depth++) {
        size_t n = 0;
        for (int i = 0; i < depth; i++) {
            deep[n++] = '[';
        }
        for (int i = 0; i < depth; i++) {
            deep[n++] = ']';
        }
        deep[n] = '\0';
        int err = parse(deep, &j);
        CHECK_INT_EQ(err, depth <= CAPS_JSON_DEPTH ? 0 : ERR_INVALID);
        if (err == 0) {
            cJSON_Delete(j);
        }
    }
}

static void
test_getters(void)
{
    cJSON *j = NULL;
    CHECK_INT_EQ(parse("{\"i\":42,\"neg\":-3,\"f\":1.5,\"big\":1e300,"
                       "\"b\":true,\"s\":\"text\",\"n\":null,"
                       "\"a\":[1,2,3],\"o\":{}}",
                       &j),
                 0);
    if (j == NULL) {
        return;
    }
    int i = -1;
    CHECK_INT_EQ(json_get_int(j, "i", 0, 100, &i), 0);
    CHECK_INT_EQ(i, 42);
    CHECK_INT_EQ(json_get_int(j, "neg", -10, 10, &i), 0);
    CHECK_INT_EQ(i, -3);
    CHECK_INT_EQ(json_get_int(j, "i", 0, 10, &i), ERR_RANGE);
    CHECK_INT_EQ(json_get_int(j, "f", 0, 10, &i), ERR_INVALID);
    CHECK_INT_EQ(json_get_int(j, "big", 0, 10, &i), ERR_RANGE);
    CHECK_INT_EQ(json_get_int(j, "s", 0, 10, &i), ERR_INVALID);
    /* Missing and null leave the value alone. */
    i = 7;
    CHECK_INT_EQ(json_get_int(j, "missing", 0, 10, &i), 0);
    CHECK_INT_EQ(json_get_int(j, "n", 0, 10, &i), 0);
    CHECK_INT_EQ(i, 7);

    double d = 0;
    CHECK_INT_EQ(json_get_double(j, "f", &d), 0);
    CHECK(d == 1.5);
    CHECK_INT_EQ(json_get_double(j, "b", &d), ERR_INVALID);

    bool b = false;
    CHECK_INT_EQ(json_get_bool(j, "b", &b), 0);
    CHECK(b);
    CHECK_INT_EQ(json_get_bool(j, "i", &b), ERR_INVALID);

    char text[8];
    CHECK_INT_EQ(json_get_text(j, "s", text, sizeof(text)), 0);
    CHECK_MEM_EQ(text, strlen(text), "text");
    CHECK_INT_EQ(json_get_text(j, "s", text, 4), ERR_LIMIT);
    CHECK_INT_EQ(json_get_text(j, "i", text, sizeof(text)), ERR_INVALID);

    const cJSON *arr;
    size_t count;
    CHECK_INT_EQ(json_get_array(j, "a", 3, &arr, &count), 0);
    CHECK_INT_EQ(count, 3);
    CHECK_INT_EQ(json_get_array(j, "a", 2, &arr, &count), ERR_LIMIT);
    CHECK_INT_EQ(json_get_array(j, "missing", 2, &arr, &count), 0);
    CHECK(arr == NULL && count == 0);
    CHECK_INT_EQ(json_get_array(j, "o", 2, &arr, &count), ERR_INVALID);

    const cJSON *o;
    CHECK_INT_EQ(json_get_object(j, "o", &o), 0);
    CHECK(o != NULL);
    CHECK_INT_EQ(json_get_object(j, "a", &o), ERR_INVALID);
    cJSON_Delete(j);
}

static void
check_clean(const char *in, size_t in_len, const char *want, const char *file,
            int line)
{
    char out[64];
    int err = str_copy_clean(str_make(in, in_len), out, sizeof(out));
    test_check_int(err, 0, "str_copy_clean", "0", file, line);
    test_check_mem(out, strlen(out), want, strlen(want), in, file, line);
}
#define CHECK_CLEAN(in, want)                                                 \
    check_clean((in), sizeof(in) - 1, (want), __FILE__, __LINE__)

static void
test_clean(void)
{
    CHECK_CLEAN("plain", "plain");
    CHECK_CLEAN("r\xc3\xa9glages \xe2\x82\xac \xf0\x9f\x93\xbb",
                "r\xc3\xa9glages \xe2\x82\xac \xf0\x9f\x93\xbb");
    /* Controls, DEL, and C1 controls (U+009B is a terminal escape). */
    CHECK_CLEAN("a\tb\nc\x1b[2J\x7f", "a?b?c?[2J?");
    CHECK_CLEAN("x\xc2\x9by", "x?y");
    /* Invalid UTF-8: stray continuation, truncated, overlong, surrogate,
     * beyond U+10FFFF. */
    CHECK_CLEAN("a\x80z", "a?z");
    CHECK_CLEAN("a\xe2\x82", "a??");
    CHECK_CLEAN("\xc0\xaf", "??");
    CHECK_CLEAN("\xed\xa0\x80", "???");
    CHECK_CLEAN("\xf4\x90\x80\x80", "????");
    /* An embedded NUL is a control character too. */
    CHECK_CLEAN("a\0b", "a?b");

    char small[4];
    CHECK_INT_EQ(str_copy_clean(STR_LIT("abc"), small, sizeof(small)), 0);
    CHECK_INT_EQ(str_copy_clean(STR_LIT("abcd"), small, sizeof(small)),
                 ERR_LIMIT);
    CHECK(small[0] == '\0');
    /* A multi-byte character that would not fit whole is refused. */
    CHECK_INT_EQ(str_copy_clean(STR_LIT("ab\xc3\xa9"), small, sizeof(small)),
                 ERR_LIMIT);
}

int
main(void)
{
    RUN(test_parse);
    RUN(test_getters);
    RUN(test_clean);
    return test_summary();
}
