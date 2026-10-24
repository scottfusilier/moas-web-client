/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* The event loop: timers, sockets, and callbacks that change the loop. */

#include "core/common.h"

#include <string.h>

#include "core/caps.h"
#include "core/loop.h"
#include "platform/clock.h"
#include "test.h"

/* Records the order in which callbacks run. */
static char order[64];
static size_t order_len;

static void
note(char c)
{
    if (order_len < sizeof(order) - 1) {
        order[order_len++] = c;
        order[order_len] = '\0';
    }
}

static void
reset_order(void)
{
    order_len = 0;
    order[0] = '\0';
}

struct named_timer {
    struct loop_timer timer;
    char name;
    int fired;
};

static void
on_named(struct loop *loop, void *arg)
{
    (void)loop;
    struct named_timer *t = arg;
    t->fired++;
    note(t->name);
}

static void
test_timers_fire_in_time_order(void)
{
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);
    struct named_timer a = {.name = 'a'};
    struct named_timer b = {.name = 'b'};
    struct named_timer c = {.name = 'c'};
    loop_timer_init(&a.timer, on_named, &a);
    loop_timer_init(&b.timer, on_named, &b);
    loop_timer_init(&c.timer, on_named, &c);
    reset_order();

    loop_timer_start(loop, &c.timer, 120, 0);
    loop_timer_start(loop, &a.timer, 20, 0);
    loop_timer_start(loop, &b.timer, 70, 0);
    CHECK(loop_timer_active(&a.timer));
    /* No sockets: loop_run() returns once the last timer has fired. */
    uint64_t start = plat_monotonic_ms();
    CHECK_INT_EQ(loop_run(loop), 0);
    CHECK_MEM_EQ(order, order_len, "abc");
    CHECK(plat_monotonic_ms() - start >= 100);
    CHECK(!loop_timer_active(&a.timer));
    loop_destroy(loop);
}

struct repeat_state {
    struct loop_timer timer;
    int count;
};

static void
on_repeat(struct loop *loop, void *arg)
{
    struct repeat_state *s = arg;
    if (++s->count == 3) {
        loop_timer_stop(loop, &s->timer); /* stop itself */
    }
}

static void
test_repeating_timer(void)
{
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);
    struct repeat_state s = {.count = 0};
    loop_timer_init(&s.timer, on_repeat, &s);

    loop_timer_start(loop, &s.timer, 0, 10);
    CHECK_INT_EQ(loop_run(loop), 0);
    CHECK_INT_EQ(s.count, 3);
    loop_destroy(loop);
}

/* a stops b, which is due at the same time: b must not fire. */
struct stopper {
    struct loop_timer timer;
    struct named_timer *victim;
};

static void
on_stopper(struct loop *loop, void *arg)
{
    struct stopper *s = arg;
    note('s');
    loop_timer_stop(loop, &s->victim->timer);
}

static void
test_stopped_timer_does_not_fire(void)
{
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);
    struct named_timer victim = {.name = 'v'};
    struct stopper s = {.victim = &victim};
    loop_timer_init(&victim.timer, on_named, &victim);
    loop_timer_init(&s.timer, on_stopper, &s);
    reset_order();

    /* Both due at once; whichever the loop picks first, v never fires. */
    loop_timer_start(loop, &victim.timer, 0, 0);
    loop_timer_start(loop, &s.timer, 0, 0);
    loop_run_once(loop, 0);
    loop_run_once(loop, 0);
    CHECK(victim.fired <= 1);
    if (victim.fired == 0) {
        CHECK_MEM_EQ(order, order_len, "s");
    } else {
        CHECK_MEM_EQ(order, order_len, "vs"); /* fired before being stopped */
    }
    loop_destroy(loop);
}

/* A timer that restarts itself with no delay runs once per pass. */
static void
on_rearm(struct loop *loop, void *arg)
{
    struct named_timer *t = arg;
    t->fired++;
    loop_timer_start(loop, &t->timer, 0, 0);
}

static void
test_zero_delay_rearm_waits_a_pass(void)
{
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);
    struct named_timer t = {.name = 'r'};
    loop_timer_init(&t.timer, on_rearm, &t);

    loop_timer_start(loop, &t.timer, 0, 0);
    for (int i = 0; i < 5; i++) {
        CHECK_INT_EQ(loop_run_once(loop, 0), 0);
    }
    CHECK_INT_EQ(t.fired, 5);
    loop_timer_stop(loop, &t.timer);
    loop_destroy(loop);
}

/* A connected pair of TCP sockets on the loopback interface. */
static bool
make_pair(plat_socket *a, plat_socket *b)
{
    struct plat_addr addr;
    plat_socket listener;

    if (plat_addr_parse("127.0.0.1:0", 11, &addr) != 0 ||
        plat_tcp_listen(&addr, &listener) != 0)
    {
        return false;
    }
    bool ok = false;
    if (plat_socket_local_addr(listener, &addr) == 0 &&
        plat_tcp_connect(&addr, a) == 0)
    {
        struct plat_pollfd p = {.sock = listener, .events = PLAT_POLLIN};
        size_t ready = 0;
        ok = plat_poll(&p, 1, 5000, &ready) == 0 && ready == 1 &&
             plat_tcp_accept(listener, b, NULL) == 0;
    }
    plat_socket_close(listener);
    return ok;
}

struct reader {
    plat_socket sock;
    char got[32];
    size_t len;
    int calls;
    bool unwatch_other;
    plat_socket other;
};

static void
on_readable(struct loop *loop, plat_socket sock, int events, void *arg)
{
    struct reader *r = arg;
    r->calls++;
    if ((events & LOOP_READ) != 0) {
        size_t n = 0;
        if (plat_recv(sock, r->got + r->len, sizeof(r->got) - r->len, &n) == 0)
        {
            r->len += n;
        }
    }
    if (r->unwatch_other) {
        loop_unwatch(loop, r->other);
    }
}

static void
test_socket_callbacks(void)
{
    plat_socket a;
    plat_socket b;
    CHECK(make_pair(&a, &b));
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);

    struct reader r = {.sock = b};
    CHECK_INT_EQ(loop_watch(loop, b, LOOP_READ, on_readable, &r), 0);
    size_t sent = 0;
    CHECK_INT_EQ(plat_send(a, "hello", 5, &sent), 0);
    for (int i = 0; i < 10 && r.len < 5; i++) {
        CHECK_INT_EQ(loop_run_once(loop, 1000), 0);
    }
    CHECK_MEM_EQ(r.got, r.len, "hello");

    /* Watching with no events: registered, but not called. */
    int calls = r.calls;
    CHECK_INT_EQ(loop_watch(loop, b, 0, on_readable, &r), 0);
    CHECK_INT_EQ(plat_send(a, "more", 4, &sent), 0);
    CHECK_INT_EQ(loop_run_once(loop, 50), 0);
    CHECK_INT_EQ(r.calls, calls);

    /* Unwatched: never called again. */
    loop_unwatch(loop, b);
    CHECK_INT_EQ(loop_run_once(loop, 50), 0);
    CHECK_INT_EQ(r.calls, calls);
    loop_unwatch(loop, b); /* twice is harmless */

    loop_destroy(loop);
    plat_socket_close(a);
    plat_socket_close(b);
}

static void
test_unwatch_during_pass(void)
{
    plat_socket a1;
    plat_socket b1;
    plat_socket a2;
    plat_socket b2;
    CHECK(make_pair(&a1, &b1));
    CHECK(make_pair(&a2, &b2));
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);

    /* Both readable in the same pass; whichever runs first unwatches the
     * other, so exactly one callback runs. */
    struct reader r1 = {.sock = b1, .unwatch_other = true, .other = b2};
    struct reader r2 = {.sock = b2, .unwatch_other = true, .other = b1};
    CHECK_INT_EQ(loop_watch(loop, b1, LOOP_READ, on_readable, &r1), 0);
    CHECK_INT_EQ(loop_watch(loop, b2, LOOP_READ, on_readable, &r2), 0);
    size_t sent = 0;
    CHECK_INT_EQ(plat_send(a1, "x", 1, &sent), 0);
    CHECK_INT_EQ(plat_send(a2, "y", 1, &sent), 0);
    plat_sleep_ms(50); /* let both arrive before the pass */
    CHECK_INT_EQ(loop_run_once(loop, 1000), 0);
    CHECK_INT_EQ(r1.calls + r2.calls, 1);

    loop_destroy(loop);
    plat_socket_close(a1);
    plat_socket_close(b1);
    plat_socket_close(a2);
    plat_socket_close(b2);
}

struct closer {
    struct loop_timer timer;
    plat_socket peer;
};

/* Closes the other end later, so the watched socket sees end of stream. */
static void
on_close_peer(struct loop *loop, void *arg)
{
    (void)loop;
    struct closer *c = arg;
    plat_socket_close(c->peer);
}

struct eof_watch {
    int events;
};

static void
on_eof(struct loop *loop, plat_socket sock, int events, void *arg)
{
    struct eof_watch *w = arg;
    char buf[8];
    size_t n;
    w->events |= events;
    if (plat_recv(sock, buf, sizeof(buf), &n) == ERR_CLOSED) {
        loop_stop(loop);
    }
}

static void
test_stop_and_hangup(void)
{
    plat_socket a;
    plat_socket b;
    CHECK(make_pair(&a, &b));
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);

    struct closer c = {.peer = a};
    struct eof_watch w = {0};
    loop_timer_init(&c.timer, on_close_peer, &c);
    loop_timer_start(loop, &c.timer, 20, 0);
    CHECK_INT_EQ(loop_watch(loop, b, LOOP_READ, on_eof, &w), 0);
    /* Runs until the callback sees end of stream and stops the loop. */
    CHECK_INT_EQ(loop_run(loop), 0);
    CHECK((w.events & LOOP_READ) != 0);

    loop_destroy(loop);
    plat_socket_close(b);
}

static void
on_never(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    (void)events;
    (void)arg;
}

static void
test_watch_limits(void)
{
    struct loop *loop;
    CHECK_INT_EQ(loop_create(&loop), 0);

    /* Bad arguments. */
    CHECK_INT_EQ(
        loop_watch(loop, PLAT_INVALID_SOCKET, LOOP_READ, on_never, NULL),
        ERR_INVALID);
    CHECK_INT_EQ(loop_watch(loop, 5, LOOP_ERROR, on_never, NULL), ERR_INVALID);
    CHECK_INT_EQ(loop_watch(loop, 5, LOOP_READ, NULL, NULL), ERR_INVALID);

    /*
     * Fill every slot.  These are just numbers, not real sockets; with
     * events 0 they are never polled.
     */
    for (int i = 0; i < CAPS_LOOP_WATCHES; i++) {
        CHECK_INT_EQ(
            loop_watch(loop, (plat_socket)(1000 + i), 0, on_never, NULL), 0);
    }
    CHECK_INT_EQ(loop_watch(loop, (plat_socket)5000, 0, on_never, NULL),
                 ERR_LIMIT);
    /* Updating an existing one still works, and a freed slot is reused. */
    CHECK_INT_EQ(loop_watch(loop, (plat_socket)1000, 0, on_never, NULL), 0);
    loop_unwatch(loop, (plat_socket)1000);
    CHECK_INT_EQ(loop_watch(loop, (plat_socket)5000, 0, on_never, NULL), 0);

    /* Nothing waits for events and no timers: run returns at once. */
    CHECK_INT_EQ(loop_run(loop), 0);
    loop_destroy(loop);
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_timers_fire_in_time_order);
    RUN(test_repeating_timer);
    RUN(test_stopped_timer_does_not_fire);
    RUN(test_zero_delay_rearm_waits_a_pass);
    RUN(test_socket_callbacks);
    RUN(test_unwatch_during_pass);
    RUN(test_stop_and_hangup);
    RUN(test_watch_limits);
    plat_net_fini();
    return test_summary();
}
