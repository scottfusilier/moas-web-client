/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Reading request heads, and the guard's rules. */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/caps.h"
#include "http/http.h"
#include "http/parse.h"
#include "test.h"

static int last_status;

static int
parse(const char *text, struct http_request *req, size_t *head_len)
{
    return http_parse_head(str_from_cstr(text), req, head_len, &last_status);
}

#define CHECK_STR(got, want) CHECK_MEM_EQ((got).ptr, (got).len, (want))

static void
test_good_requests(void)
{
    struct http_request r;
    size_t n = 0;

    const char get[] = "GET /api/state?x=1 HTTP/1.1\r\n"
                       "Host: 127.0.0.1:8080\r\n"
                       "Accept: */*\r\n"
                       "\r\n";
    CHECK_INT_EQ(parse(get, &r, &n), 0);
    CHECK_INT_EQ(n, sizeof(get) - 1);
    CHECK_STR(r.method, "GET");
    CHECK_STR(r.path, "/api/state");
    CHECK_STR(r.query, "x=1");
    CHECK_INT_EQ(r.minor_version, 1);
    CHECK(r.keep_alive);
    CHECK_INT_EQ(r.content_length, 0);
    CHECK_STR(http_header_value(&r, "host"), "127.0.0.1:8080");
    CHECK(http_header_value(&r, "Origin").ptr == NULL);

    const char post[] = "POST /api/select HTTP/1.1\r\n"
                        "Host: localhost:8080\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 42\r\n"
                        "Connection: close\r\n"
                        "\r\n";
    CHECK_INT_EQ(parse(post, &r, &n), 0);
    CHECK_INT_EQ(r.content_length, 42);
    CHECK(!r.keep_alive);
    CHECK_INT_EQ(r.query.len, 0);

    /* HTTP/1.0: no Host needed; keep-alive only if asked for. */
    CHECK_INT_EQ(parse("GET / HTTP/1.0\r\n\r\n", &r, &n), 0);
    CHECK(!r.keep_alive);
    CHECK_INT_EQ(
        parse("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", &r, &n), 0);
    CHECK(r.keep_alive);

    /* Incomplete: wait for more. */
    CHECK_INT_EQ(parse("GET / HTTP/1.1\r\nHost: x\r\n", &r, &n), ERR_AGAIN);
    CHECK_INT_EQ(parse("", &r, &n), ERR_AGAIN);
}

static void
check_refused(const char *text, int want_status, int line)
{
    struct http_request r;
    size_t n;
    int err = parse(text, &r, &n);
    test_check_int(err == ERR_INVALID, 1, text, "refused", __FILE__, line);
    test_check_int(last_status, want_status, text, "status", __FILE__, line);
}
#define CHECK_REFUSED(text, status) check_refused((text), (status), __LINE__)

static void
test_refused_requests(void)
{
    CHECK_REFUSED("GET http://evil.example/ HTTP/1.1\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("GET * HTTP/1.1\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("GET /a#b HTTP/1.1\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("GET /\x80 HTTP/1.1\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("get / HTTP/1.1\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("GET / HTTP/1.1\r\n\r\n", 400); /* no Host */
    CHECK_REFUSED("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", 400);
    CHECK_REFUSED("GET / HTTP/1.1\r\nHost: x\r\n folded\r\n\r\n", 400);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n"
                  "Content-Length: 1\r\n\r\n",
                  400);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n",
                  400);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 1x\r\n\r\n",
                  400);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: "
                  "99999999999999999999\r\n\r\n",
                  400);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 999999\r\n"
                  "\r\n",
                  413);
    CHECK_REFUSED("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: "
                  "chunked\r\n\r\n",
                  501);
    CHECK_REFUSED("GET / HTTP/2.0\r\nHost: x\r\n\r\n", 400);
    CHECK_REFUSED("NOT HTTP AT ALL\r\n\r\n", 400);

    /* A head that never ends within CAPS_HTTP_HEAD. */
    size_t big = CAPS_HTTP_HEAD + 100;
    char *text = malloc(big + 1);
    CHECK(text != NULL);
    if (text != NULL) {
        memcpy(text, "GET / HTTP/1.1\r\nHost: x\r\nX: ", 28);
        memset(text + 28, 'a', big - 28);
        text[big] = '\0';
        CHECK_REFUSED(text, 431);
        free(text);
    }

    /* More headers than CAPS_HTTP_HEADERS. */
    struct buf many;
    buf_init(&many, 8192);
    buf_append_cstr(&many, "GET / HTTP/1.1\r\nHost: x\r\n");
    for (int i = 0; i < CAPS_HTTP_HEADERS; i++) {
        buf_append_cstr(&many, "X-A: b\r\n");
    }
    buf_append_cstr(&many, "\r\n");
    CHECK_REFUSED(buf_cstr(&many), 400);
    buf_fini(&many);
}

/* A request for the guard tests: the headers given, from peer. */
static int
guard(const char *head, bool loopback_only, bool local_only,
      bool peer_loopback, const char **message)
{
    struct http_request r;
    size_t n;
    int status;
    if (http_parse_head(str_from_cstr(head), &r, &n, &status) != 0) {
        return -1;
    }
    r.peer_is_loopback = peer_loopback;
    return http_guard(&r, loopback_only, local_only, NULL, message);
}

static void
test_guard(void)
{
    const char *m = NULL;

    /* Served to this computer only: the Host must name it. */
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n", true,
                       false, true, &m),
                 0);
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: LOCALHOST:8080\r\n\r\n", true,
                       false, true, &m),
                 0);
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: [::1]:8080\r\n\r\n", true,
                       false, true, &m),
                 0);
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", true,
                       false, true, &m),
                 0);
    /* DNS rebinding: a name another site controls, pointed at us. */
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: evil.example:8080\r\n\r\n",
                       true, false, true, &m),
                 403);
    CHECK_MEM_EQ(m, strlen(m),
                 "this page is only served to the computer it runs on");
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: 192.168.1.10:8080\r\n\r\n",
                       true, false, true, &m),
                 403);
    /* Served to the network: any Host. */
    CHECK_INT_EQ(guard("GET / HTTP/1.1\r\nHost: moasweb.local\r\n\r\n", false,
                       false, false, &m),
                 0);

    /* POST: the page's own origin, and JSON. */
    const char *ok_post = "POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080"
                          "\r\nOrigin: http://127.0.0.1:8080\r\n"
                          "Content-Type: application/json; charset=utf-8"
                          "\r\n\r\n";
    CHECK_INT_EQ(guard(ok_post, true, false, true, &m), 0);
    CHECK_INT_EQ(guard("POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n"
                       "Content-Type: application/json\r\n\r\n",
                       true, false, true, &m),
                 0); /* no Origin at all: allowed, as in the Go version */
    CHECK_INT_EQ(guard("POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n"
                       "Origin: http://evil.example\r\n"
                       "Content-Type: application/json\r\n\r\n",
                       true, false, true, &m),
                 403);
    CHECK_MEM_EQ(m, strlen(m), "request from another site refused");
    CHECK_INT_EQ(
        guard("POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n"
              "Origin: null\r\nContent-Type: application/json\r\n\r\n",
              true, false, true, &m),
        403);
    CHECK_INT_EQ(guard("POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n"
                       "Content-Type: text/plain\r\n\r\n",
                       true, false, true, &m),
                 415);
    CHECK_INT_EQ(guard("POST /api/select HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n"
                       "\r\n",
                       true, false, true, &m),
                 415);

    /* Local-only routes refuse other computers, even with a good Host. */
    CHECK_INT_EQ(guard("GET /api/settings HTTP/1.1\r\nHost: x\r\n\r\n", false,
                       true, false, &m),
                 403);
    CHECK_INT_EQ(guard("GET /api/settings HTTP/1.1\r\nHost: x\r\n\r\n", false,
                       true, true, &m),
                 0);
}

int
main(void)
{
    RUN(test_good_requests);
    RUN(test_refused_requests);
    RUN(test_guard);
    return test_summary();
}
