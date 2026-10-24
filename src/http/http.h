/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A small HTTP/1.1 server for moasweb's page, run by the event loop.
 *
 * It serves exactly the routes it is given, checks every request against
 * the rules in guard.c before a handler sees it, and keeps every size and
 * time within limits (core/caps.h and the options below).  Requests come one
 * at a time per connection; connections are kept open between requests
 * when the browser asks.
 *
 * A handler answers through its struct http_reply, at once or later (when
 * it has had to wait for something).  It can also turn the reply into a
 * stream of server-sent events, which stays open for live updates.
 *
 * Callbacks into the application (handlers, and cancel callbacks) only ever
 * run from the event loop, never from inside a call the application makes
 * to this server, so the application can change its own state freely while
 * calling it.
 */

#ifndef MOAS_HTTP_HTTP_H
#define MOAS_HTTP_HTTP_H

#include "core/common.h"
#include "core/loop.h"
#include "core/str.h"
#include "http/parse.h"
#include "platform/net.h"

/* Replies. */

struct http_reply;

/*
 * Called when the browser goes away before the reply is sent, or, for a
 * stream, when the stream ends for any reason but http_stream_close().
 * After it, the reply or stream must not be used.  It is also called for
 * everything still open when the server is destroyed.
 */
typedef void http_cancel_fn(void *arg);
void http_reply_on_cancel(struct http_reply *reply, http_cancel_fn *fn,
                          void *arg);

/*
 * Adds a header to the response, such as a Content-Security-Policy for a
 * page.  name and value are copied.  At most CAPS_HTTP_EXTRA_HEADERS.
 */
int http_reply_header(struct http_reply *reply, const char *name,
                      const char *value);

/*
 * Sends the response and finishes the reply; the reply must not be used
 * again.  Every response says Cache-Control: no-store and carries the
 * usual protective headers.  For a HEAD request the body is left out.  A
 * 204 has no body.
 */
int http_reply_send(struct http_reply *reply, int status,
                    const char *content_type, const void *body, size_t len);

/* Sends a plain-text error message, as the Go version's http.Error. */
int http_reply_error(struct http_reply *reply, int status,
                     const char *message);

/* Streams. */

struct http_stream;

/*
 * Turns the reply into a stream of server-sent events (Content-Type:
 * text/event-stream) and sends its headers.  The reply's cancel callback
 * now belongs to the stream.  ERR_LIMIT, with a 503 sent, if
 * CAPS_HTTP_STREAMS are already open.
 */
int http_reply_stream(struct http_reply *reply, struct http_stream **out);

/*
 * Sends one event whose data is text (a line break in it starts a new
 * "data:" line, as the format requires).  If the browser has fallen so far
 * behind that CAPS_HTTP_SEND bytes are waiting, the stream is ended
 * (ERR_LIMIT) and its cancel callback runs on the loop's next pass.
 */
int http_stream_send(struct http_stream *stream, struct str text);

/* Ends a stream from this side; its cancel callback is not called. */
void http_stream_close(struct http_stream *stream);

/* Routes. */

/*
 * Answers one request.  req, and the slices in it, are only valid during
 * the call: copy what is needed later.
 */
typedef void http_handler_fn(struct http_reply *reply,
                             const struct http_request *req, void *arg);

struct http_route_options {
    size_t max_body; /* largest body accepted; 0: 4096 bytes */
    bool local_only; /* only from the computer the server runs on */
    const char *local_only_message; /* the 403 text; NULL for a default */
};

struct http_router;

int http_router_create(struct http_router **out);
void http_router_destroy(struct http_router *router);

/*
 * Adds a route for an exact path.  A GET route also answers HEAD.  options
 * may be NULL.  ERR_LIMIT past CAPS_HTTP_ROUTES.
 */
int http_router_add(struct http_router *router, const char *method,
                    const char *path, const struct http_route_options *options,
                    http_handler_fn *fn, void *arg);

/* The server. */

struct http_server_options {
    struct plat_addr listen;
    uint32_t head_timeout_ms;  /* to send a request's head; 0: 10 s */
    uint32_t idle_timeout_ms;  /* between requests; 0: 30 s */
    uint32_t write_timeout_ms; /* without any progress sending; 0: 30 s */
    uint32_t keepalive_ms;     /* between keep-alive comments on streams;
                                  0: 20 s */
};

struct http_server;

/*
 * Starts listening.  If the listening address is a loopback address, the
 * page is served to this computer only, and requests are refused unless
 * they are addressed to a loopback name: that stops another web site from
 * reaching the page through a DNS name it controls.  The router must
 * outlive the server.
 */
int http_server_create(struct loop *loop,
                       const struct http_server_options *options,
                       const struct http_router *router,
                       struct http_server **out);

/* Closes every connection (running cancel callbacks) and stops listening. */
void http_server_destroy(struct http_server *server);

/* Where the server listens (with the real port, if 0 was asked for). */
const struct plat_addr *http_server_addr(const struct http_server *server);

/*
 * The checks every request goes through before its handler (guard.c).
 * Returns 0, or the status to refuse with and *message.  Exposed for tests.
 */
int http_guard(const struct http_request *req, bool loopback_only,
               bool local_only, const char *local_only_message,
               const char **message);

#endif /* MOAS_HTTP_HTTP_H */
