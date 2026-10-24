/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* The platform layer's sockets, over the loopback interface. */

#include "core/common.h"

#include <string.h>

#include "platform/net.h"
#include "test.h"

static int
parse(const char *text, struct plat_addr *addr)
{
    return plat_addr_parse(text, strlen(text), addr);
}

/* Parses text, formats it again, and compares with want. */
static void
check_round_trip(const char *text, const char *want, const char *file,
                 int line)
{
    struct plat_addr addr;
    char out[PLAT_ADDR_TEXT_MAX];

    int err = parse(text, &addr);
    test_check_int(err, 0, text, "0", file, line);
    if (err == 0) {
        plat_addr_format(&addr, out);
        test_check_mem(out, strlen(out), want, strlen(want), text, file, line);
    }
}
#define CHECK_ROUND_TRIP(text, want)                                          \
    check_round_trip(text, want, __FILE__, __LINE__)

static void
test_addr_parse(void)
{
    struct plat_addr addr;

    CHECK_ROUND_TRIP("127.0.0.1:12059", "127.0.0.1:12059");
    CHECK_ROUND_TRIP("192.168.1.10:12059", "192.168.1.10:12059");
    CHECK_ROUND_TRIP("0.0.0.0:8080", "0.0.0.0:8080");
    CHECK_ROUND_TRIP("[::1]:8080", "[::1]:8080");
    CHECK_ROUND_TRIP("localhost:4532", "127.0.0.1:4532");
    CHECK_ROUND_TRIP("LocalHost:0", "127.0.0.1:0");
    CHECK_ROUND_TRIP("10.0.0.1:65535", "10.0.0.1:65535");

    CHECK_INT_EQ(parse("127.0.0.1:12059", &addr), 0);
    CHECK_INT_EQ(plat_addr_port(&addr), 12059);

    /* Not addresses. */
    const char *bad[] = {
        "",
        "12060",           /* no host */
        ":8080",           /* empty host */
        "127.0.0.1",       /* no port */
        "127.0.0.1:",      /* empty port */
        "127.0.0.1:65536", /* port too big */
        "127.0.0.1:-1",
        "127.0.0.1:80x",
        "127.0.0.1:123456",
        "256.0.0.1:80",
        "1.2.3:80",
        "::1:80", /* IPv6 needs brackets */
        "[::1]80",
        "[::1:80",
        "[]:80",
        "moas-server:12059", /* names are not looked up */
        "127.0.0.1:80:90",
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        int err = parse(bad[i], &addr);
        test_check_int(err, ERR_ADDR, bad[i], "ERR_ADDR", __FILE__, __LINE__);
    }
    /* An embedded NUL is not hidden by the length. */
    CHECK_INT_EQ(plat_addr_parse("127.0.0.1:80\0junk", 17, &addr), ERR_ADDR);
}

static void
test_loopback(void)
{
    struct plat_addr addr;
    const char *yes[] = {"127.0.0.1:1", "127.255.0.9:1", "[::1]:1",
                         "[::ffff:127.0.0.1]:1"};
    const char *no[] = {
        "192.168.1.10:1", "0.0.0.0:1",           "[::]:1",
        "[::2]:1",        "[::ffff:10.0.0.1]:1", "128.0.0.1:1"};

    for (size_t i = 0; i < ARRAY_LEN(yes); i++) {
        CHECK(parse(yes[i], &addr) == 0 && plat_addr_is_loopback(&addr));
    }
    for (size_t i = 0; i < ARRAY_LEN(no); i++) {
        CHECK(parse(no[i], &addr) == 0 && !plat_addr_is_loopback(&addr));
    }
}

/* Waits for sock to become ready for events; returns the events seen. */
static int
wait_for(plat_socket sock, int events, int timeout_ms)
{
    struct plat_pollfd p = {.sock = sock, .events = events};
    size_t ready = 0;

    if (plat_poll(&p, 1, timeout_ms, &ready) != 0 || ready == 0) {
        return 0;
    }
    return p.revents;
}

static void
test_tcp(void)
{
    struct plat_addr addr;
    plat_socket listener = PLAT_INVALID_SOCKET;
    plat_socket client = PLAT_INVALID_SOCKET;
    plat_socket server = PLAT_INVALID_SOCKET;

    CHECK_INT_EQ(parse("127.0.0.1:0", &addr), 0);
    CHECK_INT_EQ(plat_tcp_listen(&addr, &listener), 0);
    CHECK_INT_EQ(plat_socket_local_addr(listener, &addr), 0);
    CHECK(plat_addr_port(&addr) != 0);

    /* Nothing to accept yet. */
    CHECK_INT_EQ(plat_tcp_accept(listener, &server, NULL), ERR_AGAIN);

    CHECK_INT_EQ(plat_tcp_connect(&addr, &client), 0);
    CHECK((wait_for(client, PLAT_POLLOUT, 5000) & PLAT_POLLOUT) != 0);
    CHECK_INT_EQ(plat_socket_error(client), 0);
    CHECK((wait_for(listener, PLAT_POLLIN, 5000) & PLAT_POLLIN) != 0);

    struct plat_addr peer;
    CHECK_INT_EQ(plat_tcp_accept(listener, &server, &peer), 0);
    CHECK(plat_addr_is_loopback(&peer));

    /* A server-style message, then the reply. */
    const char msg[] = "{\"Message\":\"TR\"}";
    size_t sent = 0;
    CHECK_INT_EQ(plat_send(server, msg, sizeof(msg) - 1, &sent), 0);
    CHECK_INT_EQ(sent, sizeof(msg) - 1);
    CHECK((wait_for(client, PLAT_POLLIN, 5000) & PLAT_POLLIN) != 0);
    char got[64];
    size_t n = 0;
    CHECK_INT_EQ(plat_recv(client, got, sizeof(got), &n), 0);
    CHECK_MEM_EQ(got, n, "{\"Message\":\"TR\"}");

    /* Nothing more to read: would block. */
    CHECK_INT_EQ(plat_recv(client, got, sizeof(got), &n), ERR_AGAIN);

    /* The other end closes: end of stream. */
    plat_socket_close(server);
    server = PLAT_INVALID_SOCKET;
    CHECK(wait_for(client, PLAT_POLLIN, 5000) != 0);
    CHECK_INT_EQ(plat_recv(client, got, sizeof(got), &n), ERR_CLOSED);

    plat_socket_close(client);
    plat_socket_close(listener);
}

static void
test_listen_conflict(void)
{
    struct plat_addr addr;
    plat_socket a = PLAT_INVALID_SOCKET;
    plat_socket b = PLAT_INVALID_SOCKET;

    CHECK_INT_EQ(parse("127.0.0.1:0", &addr), 0);
    CHECK_INT_EQ(plat_tcp_listen(&addr, &a), 0);
    CHECK_INT_EQ(plat_socket_local_addr(a, &addr), 0);
    /* A second listener on the same port is refused, on every system. */
    CHECK_INT_EQ(plat_tcp_listen(&addr, &b), ERR_ADDR_IN_USE);
    plat_socket_close(b);
    plat_socket_close(a);
}

static void
test_connect_refused(void)
{
    struct plat_addr addr;
    plat_socket listener = PLAT_INVALID_SOCKET;
    plat_socket client = PLAT_INVALID_SOCKET;

    /* Find a free port by listening on one, then stop listening. */
    CHECK_INT_EQ(parse("127.0.0.1:0", &addr), 0);
    CHECK_INT_EQ(plat_tcp_listen(&addr, &listener), 0);
    CHECK_INT_EQ(plat_socket_local_addr(listener, &addr), 0);
    plat_socket_close(listener);

    int err = plat_tcp_connect(&addr, &client);
    if (err == 0) {
        /* Windows retries for about two seconds before giving up. */
        CHECK(wait_for(client, PLAT_POLLOUT, 10000) != 0);
        err = plat_socket_error(client);
        plat_socket_close(client);
    }
    CHECK_INT_EQ(err, ERR_REFUSED);
}

static void
test_udp(void)
{
    struct plat_addr addr;
    plat_socket rx = PLAT_INVALID_SOCKET;
    plat_socket tx = PLAT_INVALID_SOCKET;

    CHECK_INT_EQ(parse("127.0.0.1:0", &addr), 0);
    CHECK_INT_EQ(plat_udp_bind(&addr, &rx), 0);
    CHECK_INT_EQ(plat_socket_local_addr(rx, &addr), 0);
    CHECK_INT_EQ(plat_udp_open(&addr, &tx), 0);

    char got[16];
    size_t n = 0;
    CHECK_INT_EQ(plat_recvfrom(rx, got, sizeof(got), &n, NULL), ERR_AGAIN);

    const char packet[] = "<RadioInfo/>";
    CHECK_INT_EQ(plat_sendto(tx, packet, sizeof(packet) - 1, &addr), 0);
    CHECK((wait_for(rx, PLAT_POLLIN, 5000) & PLAT_POLLIN) != 0);
    struct plat_addr from;
    CHECK_INT_EQ(plat_recvfrom(rx, got, sizeof(got), &n, &from), 0);
    CHECK_MEM_EQ(got, n, "<RadioInfo/>");
    CHECK(plat_addr_is_loopback(&from));

    /* A datagram bigger than the buffer is reported, not silently cut. */
    const char big[] = "<RadioInfo><Freq>1402550</Freq></RadioInfo>";
    CHECK_INT_EQ(plat_sendto(tx, big, sizeof(big) - 1, &addr), 0);
    CHECK((wait_for(rx, PLAT_POLLIN, 5000) & PLAT_POLLIN) != 0);
    CHECK_INT_EQ(plat_recvfrom(rx, got, sizeof(got), &n, NULL), ERR_LIMIT);

    plat_socket_close(tx);
    plat_socket_close(rx);
}

static void
test_poll_arguments(void)
{
    struct plat_pollfd p;
    size_t ready;

    CHECK_INT_EQ(plat_poll(&p, 0, 0, &ready), ERR_INVALID);
    CHECK_INT_EQ(plat_poll(&p, PLAT_POLL_MAX + 1, 0, &ready), ERR_INVALID);
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_addr_parse);
    RUN(test_loopback);
    RUN(test_tcp);
    RUN(test_listen_conflict);
    RUN(test_connect_refused);
    RUN(test_udp);
    RUN(test_poll_arguments);
    plat_net_fini();
    return test_summary();
}
