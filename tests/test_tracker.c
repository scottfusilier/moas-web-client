/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Choosing between a radio's sources. */

#include "core/common.h"

#include <string.h>

#include "freq/tracker.h"
#include "test.h"

static uint64_t fake_now;

static uint64_t
clock_fn(void *arg)
{
    (void)arg;
    return fake_now;
}

static int changes;

static void
on_changed(struct freq_tracker *t, void *arg)
{
    (void)t;
    (void)arg;
    changes++;
}

static void
check_choice(struct freq_tracker *t, int want_index, int want_khz,
             const char *what, int line)
{
    int khz = 0;
    int index = freq_tracker_choose(t, &khz, NULL);
    test_check_int(index, want_index, what, "index", __FILE__, line);
    if (index >= 0) {
        test_check_int(khz, want_khz, what, "kHz", __FILE__, line);
    }
}
#define CHECK_CHOICE(t, index, khz, what)                                     \
    check_choice((t), (index), (khz), (what), __LINE__)

static void
test_priority_and_failover(void)
{
    struct loop *loop;
    struct freq_tracker *t;
    struct freq_feed first;
    struct freq_feed second;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, on_changed, NULL, &t), 0);
    fake_now = 1000000;
    freq_tracker_set_clock(t, clock_fn, NULL);
    CHECK_INT_EQ(freq_tracker_add(t, "N1MM radio 1", 0, &first), 0);
    CHECK_INT_EQ(freq_tracker_add(t, "rigctld", 2000, &second), 0);

    CHECK_CHOICE(t, -1, 0, "before any reading");
    freq_feed_report(second, 7010);
    CHECK_CHOICE(t, 1, 7010, "only the second source");
    freq_feed_report(first, 14025);
    CHECK_CHOICE(t, 0, 14025, "both sources");
    freq_feed_fail(first, "gone");
    CHECK_CHOICE(t, 1, 7010, "first source failed");

    fake_now += 3000;
    int before = changes;
    freq_tracker_refresh(t);
    CHECK_CHOICE(t, -1, 0, "second source stale");
    CHECK(changes > before); /* going stale notifies */

    struct freq_status s[CAPS_FREQ_SOURCES];
    CHECK_INT_EQ(freq_tracker_status(t, s, CAPS_FREQ_SOURCES), 2);
    CHECK_INT_EQ(s[0].state, FREQ_ERROR);
    CHECK_MEM_EQ(s[0].error, strlen(s[0].error), "gone");
    CHECK_MEM_EQ(s[0].name, strlen(s[0].name), "N1MM radio 1");
    CHECK_INT_EQ(s[1].state, FREQ_STALE);
    CHECK_INT_EQ(s[1].khz, 7010);
    CHECK_MEM_EQ(freq_state_name(s[1].state),
                 strlen(freq_state_name(s[1].state)), "stale");

    freq_feed_report(first, 14030);
    CHECK_CHOICE(t, 0, 14030, "first source back");
    const char *name = NULL;
    freq_tracker_choose(t, NULL, &name);
    CHECK(name != NULL && strcmp(name, "N1MM radio 1") == 0);

    /* A repeated reading changes nothing, so it does not notify. */
    before = changes;
    freq_feed_report(first, 14030);
    CHECK_INT_EQ(changes, before);
    /* Nor does refreshing when nothing went stale. */
    freq_tracker_refresh(t);
    CHECK_INT_EQ(changes, before);

    freq_tracker_destroy(t);
    loop_destroy(loop);
}

static void
test_limits_and_cleaning(void)
{
    struct loop *loop;
    struct freq_tracker *t;
    struct freq_feed f;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    for (int i = 0; i < CAPS_FREQ_SOURCES; i++) {
        CHECK_INT_EQ(freq_tracker_add(t, "source", 0, &f), 0);
    }
    CHECK_INT_EQ(freq_tracker_add(t, "one too many", 0, &f), ERR_LIMIT);

    /* A failure reason with control characters, and one far too long. */
    freq_feed_fail(f, "line one\nline two\x1b[2J");
    struct freq_status s[CAPS_FREQ_SOURCES];
    freq_tracker_status(t, s, CAPS_FREQ_SOURCES);
    const struct freq_status *last = &s[CAPS_FREQ_SOURCES - 1];
    CHECK_MEM_EQ(last->error, strlen(last->error), "line one?line two?[2J");
    char long_reason[CAPS_FREQ_ERROR * 3];
    memset(long_reason, 'x', sizeof(long_reason) - 1);
    long_reason[sizeof(long_reason) - 1] = '\0';
    freq_feed_fail(f, long_reason);
    freq_tracker_status(t, s, CAPS_FREQ_SOURCES);
    CHECK(strlen(last->error) > 0 && strlen(last->error) < CAPS_FREQ_ERROR);
    /* An empty reason still shows as an error. */
    freq_feed_fail(f, "");
    freq_tracker_status(t, s, CAPS_FREQ_SOURCES);
    CHECK_INT_EQ(last->state, FREQ_ERROR);

    freq_tracker_destroy(t);
    loop_destroy(loop);
}

/* With the real clock, the tracker's own timer notices staleness. */
static void
test_timer_notices_stale(void)
{
    struct loop *loop;
    struct freq_tracker *t;
    struct freq_feed f;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, on_changed, NULL, &t), 0);
    CHECK_INT_EQ(freq_tracker_add(t, "rigctld", 100, &f), 0);
    freq_feed_report(f, 3525);
    CHECK_CHOICE(t, 0, 3525, "fresh reading");
    int before = changes;
    for (int i = 0; i < 40 && changes == before; i++) {
        loop_run_once(loop, 50);
    }
    CHECK(changes > before);
    CHECK_CHOICE(t, -1, 0, "after going stale");

    freq_tracker_destroy(t);
    loop_destroy(loop);
}

int
main(void)
{
    RUN(test_priority_and_failover);
    RUN(test_limits_and_cleaning);
    RUN(test_timer_notices_stale);
    return test_summary();
}
