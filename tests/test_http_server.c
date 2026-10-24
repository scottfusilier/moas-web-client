/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* The HTTP server, end to end, with a test client on the same loop. */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/caps.h"
#include "http/http.h"
#include "platform/clock.h"
#include "test.h"

/* A minimal client: one connection, read without blocking. */
struct client {
    plat_socket sock;
    struct buf in;
    bool closed;
};

static struct loop *loop;

static void
pump(struct client *c)
{
    loop_run_once(loop, 5);
    for (;;) {
        if (buf_reserve(&c->in, 65536) != 0) {
            return;
        }
        size_t got = 0;
        int err = plat_recv(c->sock, buf_tail(&c->in), 65536, &got);
        if (err == ERR_AGAIN) {
            return;
        }
        if (err != 0) {
            c->closed = true;
            return;
        }
        buf_commit(&c->in, got);
    }
}

static bool
client_open(struct client *c, const struct plat_addr *addr)
{
    memset(c, 0, sizeof(*c));
    buf_init(&c->in, 64 * 1024 * 1024);
    if (plat_tcp_connect(addr, &c->sock) != 0) {
        return false;
    }
    uint64_t end = plat_monotonic_ms() + 3000;
    while (plat_monotonic_ms() < end) {
        loop_run_once(loop, 5);
        struct plat_pollfd p = {.sock = c->sock, .events = PLAT_POLLOUT};
        size_t ready = 0;
        if (plat_poll(&p, 1, 0, &ready) == 0 && ready == 1) {
            return plat_socket_error(c->sock) == 0;
        }
    }
    return false;
}

static void
client_close(struct client *c)
{
    plat_socket_close(c->sock);
    buf_fini(&c->in);
}

static void
client_send(struct client *c, const char *text)
{
    size_t len = strlen(text);
    while (len > 0) {
        size_t sent = 0;
        int err = plat_send(c->sock, text, len, &sent);
        if (err == ERR_AGAIN) {
            loop_run_once(loop, 5);
            continue;
        }
        if (err != 0) {
            return;
        }
        text += sent;
        len -= sent;
    }
}

/* One response, copied out of the client's input. */
struct response {
    int status;
    char head[2048];
    char body[4096];
    size_t body_len;
};

/* Waits for a whole response; expect_body false for HEAD requests. */
static bool
client_response(struct client *c, struct response *r, bool expect_body)
{
    memset(r, 0, sizeof(*r));
    uint64_t end = plat_monotonic_ms() + 3000;
    while (plat_monotonic_ms() < end) {
        pump(c);
        const char *data = buf_cstr(&c->in);
        const char *blank = strstr(data, "\r\n\r\n");
        if (blank == NULL) {
            if (c->closed) {
                return false;
            }
            continue;
        }
        size_t head_len = (size_t)(blank - data) + 4;
        size_t body_len = 0;
        const char *cl = strstr(data, "Content-Length: ");
        if (cl != NULL && cl < blank && expect_body) {
            body_len = (size_t)strtoul(cl + 16, NULL, 10);
        }
        if (c->in.len < head_len + body_len) {
            continue;
        }
        sscanf(data, "HTTP/1.1 %d", &r->status);
        size_t h = head_len < sizeof(r->head) ? head_len : sizeof(r->head) - 1;
        memcpy(r->head, data, h);
        r->head[h] = '\0';
        size_t b = body_len < sizeof(r->body) ? body_len : sizeof(r->body) - 1;
        memcpy(r->body, data + head_len, b);
        r->body[b] = '\0';
        r->body_len = body_len;
        buf_consume(&c->in, head_len + body_len);
        return true;
    }
    return false;
}

/* Waits until the server closes the connection. */
static bool
client_sees_close(struct client *c, int timeout_ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)timeout_ms;
    while (!c->closed && plat_monotonic_ms() < end) {
        pump(c);
    }
    return c->closed;
}

/* The handlers. */

static void
on_page(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    (void)arg;
    http_reply_header(reply, "Content-Security-Policy", "default-src 'self'");
    http_reply_send(reply, 200, "text/html; charset=utf-8", "<p>hi</p>", 9);
}

static void
on_echo(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)arg;
    http_reply_send(reply, 200, "application/json", req->body.ptr,
                    req->body.len);
}

static void
on_nothing(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    (void)arg;
    http_reply_send(reply, 204, NULL, NULL, 0);
}

/* A handler that answers later; the test answers for it. */
static struct http_reply *pending;
static int cancels;

static void
on_cancel(void *arg)
{
    (void)arg;
    cancels++;
    pending = NULL;
}

static void
on_later(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    (void)arg;
    pending = reply;
    http_reply_on_cancel(reply, on_cancel, NULL);
}

static struct http_stream *streams[CAPS_HTTP_STREAMS + 2];
static size_t stream_count;
static int stream_cancels;

static void
on_stream_cancel(void *arg)
{
    struct http_stream **slot = arg;
    *slot = NULL;
    stream_cancels++;
}

static void
on_events(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    (void)arg;
    struct http_stream **slot = &streams[stream_count++];
    http_reply_on_cancel(reply, on_stream_cancel, slot);
    if (http_reply_stream(reply, slot) != 0) {
        *slot = NULL;
    }
}

static struct http_router *router;
static struct http_server *server;

static void
start(uint32_t head_timeout_ms, uint32_t keepalive_ms)
{
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(http_router_create(&router), 0);
    struct http_route_options small = {.max_body = 8};
    struct http_route_options local = {
        .local_only = true,
        .local_only_message = "settings only here",
    };
    http_router_add(router, "GET", "/", NULL, on_page, NULL);
    http_router_add(router, "POST", "/echo", NULL, on_echo, NULL);
    http_router_add(router, "POST", "/small", &small, on_echo, NULL);
    http_router_add(router, "GET", "/nothing", NULL, on_nothing, NULL);
    http_router_add(router, "GET", "/later", NULL, on_later, NULL);
    http_router_add(router, "GET", "/events", NULL, on_events, NULL);
    http_router_add(router, "GET", "/local", &local, on_nothing, NULL);
    struct http_server_options opts = {
        .head_timeout_ms = head_timeout_ms,
        .keepalive_ms = keepalive_ms,
    };
    plat_addr_parse("127.0.0.1:0", 11, &opts.listen);
    CHECK_INT_EQ(http_server_create(loop, &opts, router, &server), 0);
    pending = NULL;
    cancels = 0;
    stream_count = 0;
    stream_cancels = 0;
    memset(streams, 0, sizeof(streams));
}

static void
stop(void)
{
    http_server_destroy(server);
    http_router_destroy(router);
    loop_destroy(loop);
}

#define HOST "Host: 127.0.0.1\r\n"
#define JSON "Content-Type: application/json\r\n"

static void
test_basic_responses(void)
{
    struct client c;
    struct response r;

    start(0, 0);
    CHECK(client_open(&c, http_server_addr(server)));

    client_send(&c, "GET / HTTP/1.1\r\n" HOST "\r\n");
    CHECK(client_response(&c, &r, true));
    CHECK_INT_EQ(r.status, 200);
    CHECK_MEM_EQ(r.body, r.body_len, "<p>hi</p>");
    CHECK(strstr(r.head, "Content-Type: text/html; charset=utf-8\r\n"));
    CHECK(strstr(r.head, "Content-Length: 9\r\n"));
    CHECK(strstr(r.head, "Cache-Control: no-store\r\n"));
    CHECK(strstr(r.head, "X-Content-Type-Options: nosniff\r\n"));
    CHECK(strstr(r.head, "X-Frame-Options: DENY\r\n"));
    /* The page's own policy, and not the default one as well. */
    CHECK(strstr(r.head, "Content-Security-Policy: default-src 'self'\r\n"));
    CHECK(strstr(r.head, "default-src 'none'") == NULL);
    CHECK(strstr(r.head, "Connection: close") == NULL);

    /* Same connection (keep-alive): HEAD has the length but no body. */
    client_send(&c, "HEAD / HTTP/1.1\r\n" HOST "\r\n");
    CHECK(client_response(&c, &r, false));
    CHECK_INT_EQ(r.status, 200);
    CHECK(strstr(r.head, "Content-Length: 9\r\n"));
    CHECK_INT_EQ(c.in.len, 0);

    /* JSON in, JSON out; other responses get the strict default policy. */
    client_send(&c, "POST /echo HTTP/1.1\r\n" HOST JSON
                    "Content-Length: 11\r\n\r\n{\"tx\":true}");
    CHECK(client_response(&c, &r, true));
    CHECK_INT_EQ(r.status, 200);
    CHECK_MEM_EQ(r.body, r.body_len, "{\"tx\":true}");
    CHECK(strstr(r.head, "default-src 'none'"));

    client_send(&c, "GET /nothing HTTP/1.1\r\n" HOST "\r\n");
    CHECK(client_response(&c, &r, true));
    CHECK_INT_EQ(r.status, 204);
    CHECK(strstr(r.head, "Content-Length") == NULL);

    /* Two requests sent together get two answers, in order. */
    client_send(&c, "GET /nothing HTTP/1.1\r\n" HOST "\r\n"
                    "GET / HTTP/1.1\r\n" HOST "\r\n");
    CHECK(client_response(&c, &r, true));
    CHECK_INT_EQ(r.status, 204);
    CHECK(client_response(&c, &r, true));
    CHECK_INT_EQ(r.status, 200);

    /* Asking to close: answered, then closed. */
    client_send(&c, "GET / HTTP/1.1\r\n" HOST "Connection: close\r\n\r\n");
    CHECK(client_response(&c, &r, true));
    CHECK(strstr(r.head, "Connection: close\r\n"));
    CHECK(client_sees_close(&c, 3000));
    client_close(&c);
    stop();
}

/* Sends one request on a new connection; returns the status. */
static int
one_request(const char *request, struct response *r)
{
    struct client c;
    int status = -1;
    if (client_open(&c, http_server_addr(server))) {
        client_send(&c, request);
        if (client_response(&c, r, true)) {
            status = r->status;
        }
        client_close(&c);
    }
    return status;
}

static void
test_refusals(void)
{
    struct response r;

    start(0, 0);
    CHECK_INT_EQ(one_request("GET /nowhere HTTP/1.1\r\n" HOST "\r\n", &r),
                 404);
    CHECK_INT_EQ(one_request("POST / HTTP/1.1\r\n" HOST JSON
                             "Content-Length: 0\r\n\r\n",
                             &r),
                 405);
    CHECK(strstr(r.head, "Allow: GET\r\n"));
    CHECK_INT_EQ(
        one_request("GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n", &r), 403);
    CHECK_MEM_EQ(r.body, r.body_len,
                 "this page is only served to the computer it runs on\n");
    CHECK_INT_EQ(one_request("POST /echo HTTP/1.1\r\n" HOST
                             "Content-Type: text/plain\r\n"
                             "Content-Length: 2\r\n\r\n{}",
                             &r),
                 415);
    CHECK_INT_EQ(one_request("POST /echo HTTP/1.1\r\n" HOST JSON
                             "Origin: http://evil.example\r\n"
                             "Content-Length: 2\r\n\r\n{}",
                             &r),
                 403);
    CHECK_INT_EQ(one_request("POST /small HTTP/1.1\r\n" HOST JSON
                             "Content-Length: 9\r\n\r\n123456789",
                             &r),
                 413);
    CHECK_INT_EQ(one_request("POST /echo HTTP/1.1\r\n" HOST JSON
                             "Transfer-Encoding: chunked\r\n\r\n",
                             &r),
                 501);
    CHECK_INT_EQ(one_request("GARBAGE\r\n\r\n", &r), 400);
    CHECK(strstr(r.head, "Connection: close\r\n"));
    /* Local-only, from this computer: allowed. */
    CHECK_INT_EQ(one_request("GET /local HTTP/1.1\r\n" HOST "\r\n", &r), 204);
    stop();
}

static void
test_later_and_cancel(void)
{
    struct client c;
    struct response r;

    start(0, 0);
    CHECK(client_open(&c, http_server_addr(server)));
    client_send(&c, "GET /later HTTP/1.1\r\n" HOST "\r\n");
    for (int i = 0; i < 100 && pending == NULL; i++) {
        pump(&c);
    }
    CHECK(pending != NULL);
    /* Nothing has been sent yet. */
    pump(&c);
    CHECK_INT_EQ(c.in.len, 0);
    /* The answer comes later, from outside the handler. */
    http_reply_send(pending, 200, "application/json", "{\"khz\":14025}", 13);
    pending = NULL;
    CHECK(client_response(&c, &r, true));
    CHECK_MEM_EQ(r.body, r.body_len, "{\"khz\":14025}");

    /* The browser leaves before the answer: the handler is told. */
    client_send(&c, "GET /later HTTP/1.1\r\n" HOST "\r\n");
    for (int i = 0; i < 100 && pending == NULL; i++) {
        pump(&c);
    }
    CHECK(pending != NULL);
    client_close(&c);
    for (int i = 0; i < 100 && cancels == 0; i++) {
        loop_run_once(loop, 5);
    }
    CHECK_INT_EQ(cancels, 1);
    CHECK(pending == NULL);

    /* Destroying the server tells handlers still waiting, too. */
    CHECK(client_open(&c, http_server_addr(server)));
    client_send(&c, "GET /later HTTP/1.1\r\n" HOST "\r\n");
    for (int i = 0; i < 100 && pending == NULL; i++) {
        pump(&c);
    }
    CHECK(pending != NULL);
    stop();
    CHECK_INT_EQ(cancels, 2);
    client_close(&c);
}

/* Waits for one event's text on an open stream. */
static bool
client_event(struct client *c, char *out, size_t size)
{
    uint64_t end = plat_monotonic_ms() + 3000;
    while (plat_monotonic_ms() < end) {
        pump(c);
        const char *data = buf_cstr(&c->in);
        const char *blank = strstr(data, "\n\n");
        if (blank != NULL) {
            size_t n = (size_t)(blank - data);
            snprintf(out, size, "%.*s", (int)n, data);
            buf_consume(&c->in, n + 2);
            return true;
        }
    }
    return false;
}

static void
test_streams(void)
{
    struct client c;
    struct response r;
    char event[256];

    start(0, 100); /* keep-alive comments every 100 ms */
    CHECK(client_open(&c, http_server_addr(server)));
    client_send(&c, "GET /events HTTP/1.1\r\n" HOST "\r\n");
    /* Headers first, with no length: the stream stays open. */
    CHECK(client_response(&c, &r, false));
    CHECK_INT_EQ(r.status, 200);
    CHECK(strstr(r.head, "Content-Type: text/event-stream\r\n"));
    CHECK(strstr(r.head, "Content-Length") == NULL);
    CHECK(streams[0] != NULL);

    CHECK_INT_EQ(http_stream_send(streams[0], STR_LIT("{\"a\":1}")), 0);
    CHECK(client_event(&c, event, sizeof(event)));
    CHECK_MEM_EQ(event, strlen(event), "data: {\"a\":1}");
    /* A line break in the text starts a new data line. */
    CHECK_INT_EQ(http_stream_send(streams[0], STR_LIT("one\r\ntwo")), 0);
    CHECK(client_event(&c, event, sizeof(event)));
    CHECK_MEM_EQ(event, strlen(event), "data: one\ndata: two");
    /* Keep-alive comments arrive while nothing else does. */
    CHECK(client_event(&c, event, sizeof(event)));
    CHECK_MEM_EQ(event, strlen(event), ": keep-alive");

    /* The browser leaves: the stream's cancel callback runs. */
    client_close(&c);
    for (int i = 0; i < 100 && stream_cancels == 0; i++) {
        loop_run_once(loop, 5);
    }
    CHECK_INT_EQ(stream_cancels, 1);
    CHECK(streams[0] == NULL);

    /* No more than CAPS_HTTP_STREAMS at once. */
    struct client many[CAPS_HTTP_STREAMS + 1];
    for (int i = 0; i <= CAPS_HTTP_STREAMS; i++) {
        CHECK(client_open(&many[i], http_server_addr(server)));
        client_send(&many[i], "GET /events HTTP/1.1\r\n" HOST "\r\n");
        /* Streams have no body to wait for; the refusal does. */
        CHECK(client_response(&many[i], &r, i == CAPS_HTTP_STREAMS));
        if (i == CAPS_HTTP_STREAMS) {
            CHECK_INT_EQ(r.status, 503);
        }
    }
    /* Closing one from this side does not call its cancel callback. */
    int before = stream_cancels;
    http_stream_close(streams[1]);
    CHECK_INT_EQ(stream_cancels, before);
    CHECK(client_sees_close(&many[0], 3000));
    for (int i = 0; i <= CAPS_HTTP_STREAMS; i++) {
        client_close(&many[i]);
    }
    stop();
}

static void
test_slow_stream_is_dropped(void)
{
    struct client c;
    struct response r;

    start(0, 0);
    CHECK(client_open(&c, http_server_addr(server)));
    client_send(&c, "GET /events HTTP/1.1\r\n" HOST "\r\n");
    CHECK(client_response(&c, &r, false));
    CHECK(streams[0] != NULL);

    /* The client stops reading; events pile up until the stream ends. */
    static char big[64 * 1024];
    memset(big, 'x', sizeof(big));
    int err = 0;
    for (int i = 0; i < 4000; i++) {
        err = http_stream_send(streams[0], str_make(big, sizeof(big)));
        if (err != 0) {
            break;
        }
        loop_run_once(loop, 0);
    }
    CHECK_INT_EQ(err, ERR_LIMIT);
    /* Its cancel callback runs on the loop's next pass, not inside send. */
    CHECK_INT_EQ(stream_cancels, 0);
    for (int i = 0; i < 20 && stream_cancels == 0; i++) {
        loop_run_once(loop, 5);
    }
    CHECK_INT_EQ(stream_cancels, 1);
    client_close(&c);
    stop();
}

static void
test_limits(void)
{
    struct client c;

    start(200, 0); /* a request's head must arrive within 200 ms */

    /* A request that never finishes is closed when its time is up. */
    CHECK(client_open(&c, http_server_addr(server)));
    client_send(&c, "GET / HT");
    uint64_t t0 = plat_monotonic_ms();
    CHECK(client_sees_close(&c, 3000));
    CHECK(plat_monotonic_ms() - t0 >= 150);
    client_close(&c);
    stop();

    /*
     * Past CAPS_HTTP_CONNECTIONS, a new connection is closed at once.  A
     * longer time limit here, so none of the others closes meanwhile.
     */
    start(3000, 0);
    static struct client open[CAPS_HTTP_CONNECTIONS + 1];
    for (int i = 0; i < CAPS_HTTP_CONNECTIONS; i++) {
        CHECK(client_open(&open[i], http_server_addr(server)));
    }
    for (int i = 0; i < 10; i++) {
        loop_run_once(loop, 5);
    }
    CHECK(client_open(&open[CAPS_HTTP_CONNECTIONS], http_server_addr(server)));
    CHECK(client_sees_close(&open[CAPS_HTTP_CONNECTIONS], 1000));
    CHECK(!open[0].closed);
    for (int i = 0; i <= CAPS_HTTP_CONNECTIONS; i++) {
        client_close(&open[i]);
    }
    stop();
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_basic_responses);
    RUN(test_refusals);
    RUN(test_later_and_cancel);
    RUN(test_streams);
    RUN(test_slow_stream_is_dropped);
    RUN(test_limits);
    plat_net_fini();
    return test_summary();
}
