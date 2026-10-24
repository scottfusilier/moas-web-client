/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "core/caps.h"
#include "core/loop.h"
#include "platform/clock.h"

_Static_assert(CAPS_LOOP_WATCHES <= PLAT_POLL_MAX,
               "CAPS_LOOP_WATCHES must not exceed PLAT_POLL_MAX");

struct watch {
    plat_socket sock;
    int events;
    loop_io_fn *fn;
    void *arg;
    /*
     * Unique per registration, 0 when the slot is free.  A callback that
     * unwatches a socket, closes it, and opens a new one that the system
     * gives the same number must not receive the old socket's events; the
     * serial tells the two apart.
     */
    uint64_t serial;
};

struct loop {
    struct watch watches[CAPS_LOOP_WATCHES];

    /* The sockets being waited for in this pass, and where they came from. */
    struct plat_pollfd pollfds[CAPS_LOOP_WATCHES];
    size_t polled_slot[CAPS_LOOP_WATCHES];
    uint64_t polled_serial[CAPS_LOOP_WATCHES];

    struct loop_timer *timers; /* active timers, in no particular order */
    uint64_t next_serial;
    uint64_t pass;
    uint64_t now;
    bool stopping;
};

int
loop_create(struct loop **out)
{
    struct loop *loop = calloc(1, sizeof(*loop));
    if (loop == NULL) {
        return ERR_NOMEM;
    }
    loop->next_serial = 1;
    loop->now = plat_monotonic_ms();
    *out = loop;
    return 0;
}

void
loop_destroy(struct loop *loop)
{
    for (struct loop_timer *t = loop->timers; t != NULL; t = t->next) {
        t->active = false;
    }
    free(loop);
}

static struct watch *
find_watch(struct loop *loop, plat_socket sock)
{
    for (size_t i = 0; i < CAPS_LOOP_WATCHES; i++) {
        struct watch *w = &loop->watches[i];
        if (w->serial != 0 && w->sock == sock) {
            return w;
        }
    }
    return NULL;
}

int
loop_watch(struct loop *loop, plat_socket sock, int events, loop_io_fn *fn,
           void *arg)
{
    if ((events & ~(LOOP_READ | LOOP_WRITE)) != 0 || fn == NULL ||
        sock == PLAT_INVALID_SOCKET)
    {
        return ERR_INVALID;
    }
    struct watch *w = find_watch(loop, sock);
    if (w == NULL) {
        for (size_t i = 0; i < CAPS_LOOP_WATCHES && w == NULL; i++) {
            if (loop->watches[i].serial == 0) {
                w = &loop->watches[i];
            }
        }
        if (w == NULL) {
            return ERR_LIMIT;
        }
        w->sock = sock;
        w->serial = loop->next_serial++;
    }
    w->events = events;
    w->fn = fn;
    w->arg = arg;
    return 0;
}

void
loop_unwatch(struct loop *loop, plat_socket sock)
{
    struct watch *w = find_watch(loop, sock);
    if (w != NULL) {
        memset(w, 0, sizeof(*w));
    }
}

void
loop_timer_init(struct loop_timer *timer, loop_timer_fn *fn, void *arg)
{
    memset(timer, 0, sizeof(*timer));
    timer->fn = fn;
    timer->arg = arg;
}

static void
unlink_timer(struct loop *loop, struct loop_timer *timer)
{
    for (struct loop_timer **p = &loop->timers; *p != NULL; p = &(*p)->next) {
        if (*p == timer) {
            *p = timer->next;
            break;
        }
    }
    timer->next = NULL;
    timer->active = false;
}

void
loop_timer_start(struct loop *loop, struct loop_timer *timer,
                 uint64_t delay_ms, uint64_t repeat_ms)
{
    if (timer->active) {
        unlink_timer(loop, timer);
    }
    uint64_t now = plat_monotonic_ms();
    timer->deadline = delay_ms > UINT64_MAX - now ? UINT64_MAX
                                                  : now + delay_ms;
    timer->repeat_ms = repeat_ms;
    timer->armed_pass = loop->pass;
    timer->active = true;
    timer->next = loop->timers;
    loop->timers = timer;
}

void
loop_timer_stop(struct loop *loop, struct loop_timer *timer)
{
    if (timer->active) {
        unlink_timer(loop, timer);
    }
}

bool
loop_timer_active(const struct loop_timer *timer)
{
    return timer->active;
}

/*
 * Fires every timer that is due and was not started in this pass.  The list
 * is searched afresh after each callback, because a callback may stop,
 * start or free any timer.
 */
static void
run_due_timers(struct loop *loop)
{
    while (!loop->stopping) {
        struct loop_timer *due = NULL;
        for (struct loop_timer *t = loop->timers; t != NULL; t = t->next) {
            if (t->deadline <= loop->now && t->armed_pass != loop->pass) {
                due = t;
                break;
            }
        }
        if (due == NULL) {
            return;
        }
        if (due->repeat_ms != 0) {
            due->deadline = loop->now + due->repeat_ms;
            due->armed_pass = loop->pass;
        } else {
            unlink_timer(loop, due);
        }
        /* After this call, due may no longer exist. */
        due->fn(loop, due->arg);
    }
}

/* Milliseconds until the next timer, or -1 if there is none. */
static int64_t
next_timer_wait(const struct loop *loop)
{
    int64_t wait = -1;
    for (const struct loop_timer *t = loop->timers; t != NULL; t = t->next) {
        uint64_t left = t->deadline > loop->now ? t->deadline - loop->now : 0;
        if (left > (uint64_t)INT_MAX) {
            left = (uint64_t)INT_MAX;
        }
        if (wait < 0 || (int64_t)left < wait) {
            wait = (int64_t)left;
        }
    }
    return wait;
}

/*
 * On a hang-up or error, Unix systems usually also report the socket
 * readable, but Windows does not.  Reporting LOOP_READ as well makes every
 * system behave the same: a callback that reads finds the end of the stream
 * or the error.
 */
static int
to_loop_events(int revents)
{
    int events = ((revents & PLAT_POLLIN) != 0 ? LOOP_READ : 0) |
                 ((revents & PLAT_POLLOUT) != 0 ? LOOP_WRITE : 0);
    if ((revents & (PLAT_POLLERR | PLAT_POLLHUP)) != 0) {
        events |= LOOP_READ | LOOP_ERROR;
    }
    return events;
}

int
loop_run_once(struct loop *loop, int max_wait_ms)
{
    loop->pass++;
    loop->now = plat_monotonic_ms();
    run_due_timers(loop);
    if (loop->stopping) {
        return 0;
    }

    size_t n = 0;
    for (size_t i = 0; i < CAPS_LOOP_WATCHES; i++) {
        const struct watch *w = &loop->watches[i];
        if (w->serial == 0 || w->events == 0) {
            continue;
        }
        loop->pollfds[n].sock = w->sock;
        loop->pollfds[n].events = ((w->events & LOOP_READ) != 0 ? PLAT_POLLIN
                                                                : 0) |
                                  ((w->events & LOOP_WRITE) != 0 ? PLAT_POLLOUT
                                                                 : 0);
        loop->pollfds[n].revents = 0;
        loop->polled_slot[n] = i;
        loop->polled_serial[n] = w->serial;
        n++;
    }

    int64_t wait = next_timer_wait(loop);
    if (max_wait_ms >= 0 && (wait < 0 || wait > max_wait_ms)) {
        wait = max_wait_ms;
    }

    if (n == 0) {
        /* Nothing to poll (and Windows refuses to poll nothing): sleep. */
        if (wait > 0) {
            plat_sleep_ms((uint32_t)wait);
        }
        loop->now = plat_monotonic_ms();
        return 0;
    }

    size_t ready = 0;
    int err = plat_poll(loop->pollfds, n, (int)wait, &ready);
    loop->now = plat_monotonic_ms();
    if (err != 0) {
        return err;
    }

    for (size_t i = 0; i < n && ready > 0 && !loop->stopping; i++) {
        if (loop->pollfds[i].revents == 0) {
            continue;
        }
        ready--;
        struct watch *w = &loop->watches[loop->polled_slot[i]];
        if (w->serial != loop->polled_serial[i]) {
            continue; /* unwatched, or the slot now holds another socket */
        }
        /* Only what the socket is still watched for, and errors. */
        int events = to_loop_events(loop->pollfds[i].revents) &
                     (w->events | LOOP_ERROR);
        if (events != 0) {
            w->fn(loop, w->sock, events, w->arg);
        }
    }
    return 0;
}

/* True if no socket is waiting for events and no timer is running. */
static bool
idle(const struct loop *loop)
{
    if (loop->timers != NULL) {
        return false;
    }
    for (size_t i = 0; i < CAPS_LOOP_WATCHES; i++) {
        if (loop->watches[i].serial != 0 && loop->watches[i].events != 0) {
            return false;
        }
    }
    return true;
}

int
loop_run(struct loop *loop)
{
    loop->stopping = false;
    while (!loop->stopping && !idle(loop)) {
        int err = loop_run_once(loop, -1);
        if (err != 0) {
            return err;
        }
    }
    loop->stopping = false;
    return 0;
}

void
loop_stop(struct loop *loop)
{
    loop->stopping = true;
}

uint64_t
loop_now(const struct loop *loop)
{
    return loop->now;
}
