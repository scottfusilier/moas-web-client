/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/str.h"
#include "freq/tracker.h"
#include "platform/clock.h"

/* How often readings are checked for going stale. */
#define REFRESH_MS 250

struct slot {
    char name[CAPS_FREQ_NAME];
    uint32_t timeout_ms;
    int khz;
    bool have_reading;
    uint64_t at; /* when khz was read */
    char error[CAPS_FREQ_ERROR];
};

struct freq_tracker {
    struct loop *loop;
    freq_changed_fn *changed;
    void *arg;
    uint64_t (*now)(void *arg);
    void *now_arg;
    struct loop_timer timer;
    freq_event_fn *on_event;
    void *event_arg;

    struct slot slots[CAPS_FREQ_SOURCES];
    size_t count;

    /* The statuses as of the last "changed" call. */
    struct freq_status shown[CAPS_FREQ_SOURCES];
};

const char *
freq_state_name(enum freq_state state)
{
    switch (state) {
    case FREQ_WAITING:
        return "waiting";
    case FREQ_UP:
        return "up";
    case FREQ_STALE:
        return "stale";
    case FREQ_ERROR:
        return "error";
    }
    return "waiting";
}

static uint64_t
system_now(void *arg)
{
    (void)arg;
    return plat_monotonic_ms();
}

static enum freq_state
state_of(const struct slot *s, uint64_t now)
{
    if (s->error[0] != '\0') {
        return FREQ_ERROR;
    }
    if (!s->have_reading) {
        return FREQ_WAITING;
    }
    if (s->timeout_ms > 0 && now > s->at && now - s->at > s->timeout_ms) {
        return FREQ_STALE;
    }
    return FREQ_UP;
}

static void
fill_status(const struct freq_tracker *t, size_t i, uint64_t now,
            struct freq_status *out)
{
    const struct slot *s = &t->slots[i];
    memcpy(out->name, s->name, sizeof(out->name));
    out->state = state_of(s, now);
    out->khz = s->khz;
    memcpy(out->error, s->error, sizeof(out->error));
}

/* Calls "changed" if any status differs from what was last shown. */
static void
notice(struct freq_tracker *t)
{
    uint64_t now = t->now(t->now_arg);
    bool differs = false;
    for (size_t i = 0; i < t->count; i++) {
        struct freq_status s;
        fill_status(t, i, now, &s);
        const struct freq_status *old = &t->shown[i];
        if (s.state != old->state || s.khz != old->khz ||
            strcmp(s.error, old->error) != 0)
        {
            t->shown[i] = s;
            differs = true;
        }
    }
    if (differs && t->changed != NULL) {
        t->changed(t, t->arg);
    }
}

static void
on_timer(struct loop *loop, void *arg)
{
    (void)loop;
    freq_tracker_refresh(arg);
}

int
freq_tracker_create(struct loop *loop, freq_changed_fn *changed, void *arg,
                    struct freq_tracker **out)
{
    struct freq_tracker *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return ERR_NOMEM;
    }
    t->loop = loop;
    t->changed = changed;
    t->arg = arg;
    t->now = system_now;
    loop_timer_init(&t->timer, on_timer, t);
    loop_timer_start(loop, &t->timer, REFRESH_MS, REFRESH_MS);
    *out = t;
    return 0;
}

void
freq_tracker_destroy(struct freq_tracker *t)
{
    if (t == NULL) {
        return;
    }
    loop_timer_stop(t->loop, &t->timer);
    free(t);
}

int
freq_tracker_add(struct freq_tracker *t, const char *name, uint32_t timeout_ms,
                 struct freq_feed *out)
{
    if (t->count >= CAPS_FREQ_SOURCES) {
        return ERR_LIMIT;
    }
    struct slot *s = &t->slots[t->count];
    memset(s, 0, sizeof(*s));
    int err = str_copy_clean(str_from_cstr(name), s->name, sizeof(s->name));
    if (err != 0) {
        return err;
    }
    s->timeout_ms = timeout_ms;
    fill_status(t, t->count, t->now(t->now_arg), &t->shown[t->count]);
    out->tracker = t;
    out->index = t->count;
    t->count++;
    return 0;
}

void
freq_feed_report(struct freq_feed feed, int khz)
{
    struct freq_tracker *t = feed.tracker;
    struct slot *s = &t->slots[feed.index];
    s->khz = khz;
    s->have_reading = true;
    s->at = t->now(t->now_arg);
    s->error[0] = '\0';
    notice(t);
}

void
freq_feed_fail(struct freq_feed feed, const char *reason)
{
    struct freq_tracker *t = feed.tracker;
    struct slot *s = &t->slots[feed.index];
    struct str r = str_from_cstr(
        reason != NULL && reason[0] != '\0' ? reason : "failed");
    /* A reason too long is cut to fit; keep at least a marker. */
    if (r.len >= sizeof(s->error)) {
        r.len = sizeof(s->error) - 4;
    }
    if (str_copy_clean(r, s->error, sizeof(s->error)) != 0) {
        memcpy(s->error, "failed", 7);
    }
    notice(t);
}

void
freq_tracker_on_event(struct freq_tracker *t, freq_event_fn *fn, void *arg)
{
    t->on_event = fn;
    t->event_arg = arg;
}

void
freq_feed_event(struct freq_feed feed, const struct freq_event *event)
{
    struct freq_tracker *t = feed.tracker;
    if (t->on_event == NULL) {
        return;
    }
    int followed = freq_tracker_choose(t, NULL, NULL);
    if (followed >= 0 && (size_t)followed == feed.index) {
        t->on_event(t, event, t->event_arg);
    }
}

int
freq_tracker_choose(const struct freq_tracker *t, int *khz, const char **name)
{
    uint64_t now = t->now(t->now_arg);
    for (size_t i = 0; i < t->count; i++) {
        if (state_of(&t->slots[i], now) == FREQ_UP) {
            if (khz != NULL) {
                *khz = t->slots[i].khz;
            }
            if (name != NULL) {
                *name = t->slots[i].name;
            }
            return (int)i;
        }
    }
    return -1;
}

size_t
freq_tracker_status(const struct freq_tracker *t, struct freq_status *out,
                    size_t max)
{
    uint64_t now = t->now(t->now_arg);
    size_t n = t->count < max ? t->count : max;
    for (size_t i = 0; i < n; i++) {
        fill_status(t, i, now, &out[i]);
    }
    return n;
}

void
freq_tracker_refresh(struct freq_tracker *t)
{
    notice(t);
}

void
freq_tracker_set_clock(struct freq_tracker *t, uint64_t (*now)(void *arg),
                       void *arg)
{
    t->now = now;
    t->now_arg = arg;
    loop_timer_stop(t->loop, &t->timer);
}
