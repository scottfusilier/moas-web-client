/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Splitting the server's stream into messages. */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/caps.h"
#include "oasp/frame.h"
#include "oasp_samples.h"
#include "test.h"

/*
 * Feeds stream to the framer in two parts, split at split, the way TCP
 * might deliver it, and counts the objects found.  Each found object must
 * be the next one in want[].
 */
static int
feed_split(const char *stream, size_t len, size_t split,
           const char *const *want, size_t want_count)
{
    struct buf b;
    struct oasp_framer f;
    int found = 0;

    buf_init(&b, len + 1);
    oasp_framer_reset(&f);
    for (int part = 0; part < 2; part++) {
        if (part == 0) {
            buf_append(&b, stream, split);
        } else {
            buf_append(&b, stream + split, len - split);
        }
        for (;;) {
            size_t start;
            size_t end;
            int err = oasp_frame_next(&f, b.data, b.len, &start, &end);
            if (err != 0) {
                CHECK_INT_EQ(err, ERR_AGAIN);
                break;
            }
            if ((size_t)found < want_count) {
                CHECK_MEM_EQ(b.data + start, end - start, want[found]);
            }
            found++;
            buf_consume(&b, end);
            oasp_framer_reset(&f);
        }
    }
    buf_fini(&b);
    return found;
}

static void
test_split_anywhere(void)
{
    /* As the real server sends them: back to back, or with newlines. */
    const char *want[] = {SAMPLE_SERVER, SAMPLE_ROTATORS, SAMPLE_CONFIGURATION,
                          SAMPLE_STATE, SAMPLE_TR};
    const char stream[] = SAMPLE_SERVER SAMPLE_ROTATORS
        "\n" SAMPLE_CONFIGURATION "\r\n  " SAMPLE_STATE "\t" SAMPLE_TR "\n";
    size_t len = sizeof(stream) - 1;

    /* Every possible split point gives the same five objects. */
    int failures = 0;
    for (size_t split = 0; split <= len; split++) {
        if (feed_split(stream, len, split, want, ARRAY_LEN(want)) != 5) {
            failures++;
        }
    }
    CHECK_INT_EQ(failures, 0);
}

/* Runs the framer over all of text at once; returns its first result. */
static int
frame_once(const char *text, size_t *start, size_t *end)
{
    struct oasp_framer f;
    oasp_framer_reset(&f);
    return oasp_frame_next(&f, text, strlen(text), start, end);
}

static void
test_strings_and_escapes(void)
{
    size_t start = 0;
    size_t end = 0;

    /* Braces and brackets inside strings do not count. */
    const char a[] = "{\"Name\":\"}{]}[\"} rest";
    CHECK_INT_EQ(frame_once(a, &start, &end), 0);
    CHECK_MEM_EQ(a + start, end - start, "{\"Name\":\"}{]}[\"}");

    /* An escaped quote does not end the string; an escaped backslash does
     * not escape the quote after it. */
    const char b[] = "{\"a\":\"x\\\"}\",\"b\":\"y\\\\\"}";
    CHECK_INT_EQ(frame_once(b, &start, &end), 0);
    CHECK_INT_EQ(end, sizeof(b) - 1);

    /* Nested objects and arrays. */
    const char c[] = "{\"a\":[{\"b\":[1,2,{}]}],\"c\":{}}{";
    CHECK_INT_EQ(frame_once(c, &start, &end), 0);
    CHECK_INT_EQ(end, sizeof(c) - 2);

    /* Leading whitespace is skipped; start is the brace. */
    CHECK_INT_EQ(frame_once(" \r\n\t{}", &start, &end), 0);
    CHECK_INT_EQ(start, 4);
    CHECK_INT_EQ(end, 6);

    /* Nothing but whitespace, or an unfinished object: wait for more. */
    CHECK_INT_EQ(frame_once("  \n", &start, &end), ERR_AGAIN);
    CHECK_INT_EQ(frame_once("{\"a\":\"}", &start, &end), ERR_AGAIN);
    CHECK_INT_EQ(frame_once("", &start, &end), ERR_AGAIN);
}

static void
test_rejects_what_is_not_an_object(void)
{
    size_t start;
    size_t end;

    CHECK_INT_EQ(frame_once("x{}", &start, &end), ERR_INVALID);
    CHECK_INT_EQ(frame_once("[]", &start, &end), ERR_INVALID);
    CHECK_INT_EQ(frame_once("}", &start, &end), ERR_INVALID);
    CHECK_INT_EQ(frame_once("\"text\"", &start, &end), ERR_INVALID);
    /* The object closed by a bracket. */
    CHECK_INT_EQ(frame_once("{\"a\":1]", &start, &end), ERR_INVALID);
    /* Garbage after a good object shows up on the next call. */
    struct oasp_framer f;
    const char two[] = "{}junk";
    oasp_framer_reset(&f);
    CHECK_INT_EQ(oasp_frame_next(&f, two, 6, &start, &end), 0);
    oasp_framer_reset(&f);
    CHECK_INT_EQ(oasp_frame_next(&f, two + end, 6 - end, &start, &end),
                 ERR_INVALID);
}

static void
test_limits(void)
{
    size_t start;
    size_t end;

    /* Nesting: CAPS_JSON_DEPTH levels are fine, one more is not. */
    char deep[CAPS_JSON_DEPTH * 2 + 4];
    size_t n = 0;
    deep[n++] = '{';
    deep[n++] = '"';
    deep[n++] = 'a';
    deep[n++] = '"';
    deep[n++] = ':';
    for (int i = 1; i < CAPS_JSON_DEPTH; i++) {
        deep[n++] = '[';
    }
    deep[n] = '\0';
    CHECK_INT_EQ(frame_once(deep, &start, &end), ERR_AGAIN);
    deep[n++] = '[';
    deep[n] = '\0';
    CHECK_INT_EQ(frame_once(deep, &start, &end), ERR_LIMIT);

    /* Size: an object that never ends stops at CAPS_OASP_MESSAGE. */
    size_t big = CAPS_OASP_MESSAGE + 16;
    char *text = malloc(big + 1);
    CHECK(text != NULL);
    if (text == NULL) {
        return;
    }
    memset(text, ' ', big);
    text[0] = '{';
    text[1] = '"';
    text[big] = '\0';
    CHECK_INT_EQ(frame_once(text, &start, &end), ERR_LIMIT);
    /* Just under the limit is still waiting, not refused. */
    text[CAPS_OASP_MESSAGE - 1] = '\0';
    CHECK_INT_EQ(frame_once(text, &start, &end), ERR_AGAIN);
    free(text);
}

int
main(void)
{
    RUN(test_split_anywhere);
    RUN(test_strings_and_escapes);
    RUN(test_rejects_what_is_not_an_object);
    RUN(test_limits);
    return test_summary();
}
