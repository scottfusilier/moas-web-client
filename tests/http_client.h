/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A minimal HTTP client for tests: one connection, on the same event loop as
 * the server being tested, never blocking it.
 */

#ifndef MOAS_TESTS_HTTP_CLIENT_H
#define MOAS_TESTS_HTTP_CLIENT_H

#include "core/buf.h"
#include "core/common.h"
#include "core/loop.h"
#include "platform/net.h"

struct client {
    struct loop *loop;
    plat_socket sock;
    struct buf in;
    bool closed;
};

/* One response, copied out of the client's input. */
struct response {
    int status;
    char head[2048];
    char *body; /* NUL-terminated, from malloc(); free with response_fini */
    size_t body_len;
};

bool client_open(struct client *c, struct loop *loop,
                 const struct plat_addr *addr);
void client_close(struct client *c);
void client_send(struct client *c, const char *text);

/* Runs the loop once and reads whatever has arrived. */
void client_pump(struct client *c);

/* Waits for a whole response; expect_body is false for HEAD requests. */
bool client_response(struct client *c, struct response *r, bool expect_body);
void response_fini(struct response *r);

/* Waits for one server-sent event's text ("data: ..." lines). */
bool client_event(struct client *c, struct buf *out);

bool client_sees_close(struct client *c, int timeout_ms);

/*
 * One request on a new connection, the way the page sends it: JSON body
 * (if any), Host 127.0.0.1.  extra_headers may be NULL.  Returns the
 * status, -1 on failure; *r holds the response (response_fini it).
 */
int http_request(struct loop *loop, const struct plat_addr *addr,
                 const char *method, const char *path, const char *body,
                 const char *extra_headers, struct response *r);

/* The same, for a request written out in full. */
int http_raw(struct loop *loop, const struct plat_addr *addr,
             const char *request, bool expect_body, struct response *r);

#endif /* MOAS_TESTS_HTTP_CLIENT_H */
