/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Socket pairs and stop requests.  A real Ctrl+C cannot be sent from a
 * test without risking the test runner itself, so plat_signal_raise()
 * stands in for it; the path from there to the event loop is the same.
 */

#include "core/common.h"

#include "core/loop.h"
#include "platform/net.h"
#include "platform/signal.h"
#include "test.h"

static void
test_socket_pair(void)
{
    plat_socket a = PLAT_INVALID_SOCKET;
    plat_socket b = PLAT_INVALID_SOCKET;

    CHECK_INT_EQ(plat_socket_pair(&a, &b), 0);
    size_t n = 0;
    CHECK_INT_EQ(plat_send(a, "ping", 4, &n), 0);
    struct plat_pollfd p = {.sock = b, .events = PLAT_POLLIN};
    size_t ready = 0;
    CHECK_INT_EQ(plat_poll(&p, 1, 5000, &ready), 0);
    char got[8];
    CHECK_INT_EQ(plat_recv(b, got, sizeof(got), &n), 0);
    CHECK_MEM_EQ(got, n, "ping");
    /* Both ways. */
    CHECK_INT_EQ(plat_send(b, "pong", 4, &n), 0);
    p.sock = a;
    CHECK_INT_EQ(plat_poll(&p, 1, 5000, &ready), 0);
    CHECK_INT_EQ(plat_recv(a, got, sizeof(got), &n), 0);
    CHECK_MEM_EQ(got, n, "pong");
    plat_socket_close(a);
    plat_socket_close(b);
}

static void
on_stop(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    int *stops = arg;
    plat_signal_drain(sock);
    (*stops)++;
    loop_stop(loop);
}

static void
test_stop_request_stops_loop(void)
{
    plat_socket notify = PLAT_INVALID_SOCKET;
    struct loop *loop;
    int stops = 0;

    CHECK_INT_EQ(plat_signal_open(&notify), 0);
    /* Only once per process. */
    plat_socket again;
    CHECK_INT_EQ(plat_signal_open(&again), ERR_INVALID);

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(loop_watch(loop, notify, LOOP_READ, on_stop, &stops), 0);
    plat_signal_raise();
    plat_signal_raise(); /* two quick requests: drained together */
    CHECK_INT_EQ(loop_run(loop), 0);
    CHECK_INT_EQ(stops, 1);

    /* Drained: nothing more to read. */
    char c;
    size_t n;
    CHECK_INT_EQ(plat_recv(notify, &c, 1, &n), ERR_AGAIN);

    loop_unwatch(loop, notify);
    loop_destroy(loop);
    plat_signal_close();
    /* After closing it can be opened again. */
    CHECK_INT_EQ(plat_signal_open(&notify), 0);
    plat_signal_close();
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_socket_pair);
    RUN(test_stop_request_stops_loop);
    plat_net_fini();
    return test_summary();
}
