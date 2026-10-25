/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Deciding which frequency report to believe for one radio.
 *
 * A tracker holds a radio's sources in priority order.  Each source reports
 * through its feed: a reading in kHz, or a failure.  The tracker follows the
 * first source that is up.  A source is
 *
 *   waiting  until its first reading,
 *   up       while its latest reading counts,
 *   stale    once that reading is older than the source's time limit,
 *   error    after a failure, until it reports again.
 *
 * Whenever what the tracker would show changes, it calls its "changed"
 * callback.  It checks for readings going stale four times a second.
 */

#ifndef MOAS_FREQ_TRACKER_H
#define MOAS_FREQ_TRACKER_H

#include "core/caps.h"
#include "core/common.h"
#include "core/loop.h"

enum freq_state {
    FREQ_WAITING,
    FREQ_UP,
    FREQ_STALE,
    FREQ_ERROR,
};

/* "waiting", "up", "stale" or "error". */
const char *freq_state_name(enum freq_state state);

/* One source, for display. */
struct freq_status {
    char name[CAPS_FREQ_NAME];
    enum freq_state state;
    int khz; /* the latest reading; 0 if none yet */
    char error[CAPS_FREQ_ERROR];
};

struct freq_tracker;

/*
 * Called after any change to what freq_tracker_choose() or
 * freq_tracker_status() would give.  It may call those, but must not
 * destroy the tracker or stop a source: it runs while a source is
 * reporting.
 */
typedef void freq_changed_fn(struct freq_tracker *tracker, void *arg);

/* The handle one source uses to report on one radio. */
struct freq_feed {
    struct freq_tracker *tracker;
    size_t index;
};

int freq_tracker_create(struct loop *loop, freq_changed_fn *changed, void *arg,
                        struct freq_tracker **out);
void freq_tracker_destroy(struct freq_tracker *tracker);

/*
 * Adds a source with lower priority than those already added.  A reading
 * older than timeout_ms no longer counts; 0 means readings never go stale.
 * ERR_LIMIT past CAPS_FREQ_SOURCES.
 */
int freq_tracker_add(struct freq_tracker *tracker, const char *name,
                     uint32_t timeout_ms, struct freq_feed *out);

/* A reading in kHz; clears any earlier failure. */
void freq_feed_report(struct freq_feed feed, int khz);

/* The source cannot read the radio; reason is shown (and cleaned). */
void freq_feed_fail(struct freq_feed feed, const char *reason);

/*
 * Besides frequencies, a logger may say what its operator did on the radio.
 * Only events from the source being followed are passed on, so that two
 * loggers never act on one radio.
 */
enum freq_event_kind {
    /* The logger moved to its next antenna (N1MM's Alt-F9); khz is the
     * radio's frequency in the same report. */
    FREQ_EVENT_ANTENNA_STEP,
    /* The logger's message for the radio changed (text; "" when none). */
    FREQ_EVENT_TEXT,
};

struct freq_event {
    enum freq_event_kind kind;
    int khz;
    const char *text;
};

/* Called for each event from the followed source; may not stop sources. */
typedef void freq_event_fn(struct freq_tracker *tracker,
                           const struct freq_event *event, void *arg);

/* Where events go (NULL: nowhere, the default). */
void freq_tracker_on_event(struct freq_tracker *tracker, freq_event_fn *fn,
                           void *arg);

/*
 * An event from a source.  Report the frequency that comes with it first,
 * so that a source that has just come up counts as followed.
 */
void freq_feed_event(struct freq_feed feed, const struct freq_event *event);

/*
 * The source to follow: the first, in priority order, that is up.  Returns
 * its index, with its reading in *khz and its name in *name (either may be
 * NULL), or -1 if no source is up.
 */
int freq_tracker_choose(const struct freq_tracker *tracker, int *khz,
                        const char **name);

/* Fills out[] with up to max statuses, in priority order; returns how many. */
size_t freq_tracker_status(const struct freq_tracker *tracker,
                           struct freq_status *out, size_t max);

/* Notices readings that have gone stale; the tracker's timer calls it. */
void freq_tracker_refresh(struct freq_tracker *tracker);

/*
 * Replaces the clock (monotonic milliseconds), for tests.  The tracker
 * then also stops checking on its own; call freq_tracker_refresh().
 */
void freq_tracker_set_clock(struct freq_tracker *tracker,
                            uint64_t (*now)(void *arg), void *arg);

#endif /* MOAS_FREQ_TRACKER_H */
