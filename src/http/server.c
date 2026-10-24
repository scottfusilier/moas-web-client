/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Connections, responses and streams.
 *
 * Each connection is in one state at a time:
 *
 *   READING    waiting for a request (the head, then the body)
 *   HANDLING   its handler has the reply and has not answered yet
 *   WRITING    sending a response; afterwards, READING again or closed
 *   STREAMING  an open stream of server-sent events
 *   CLOSING    to be closed on the loop's next pass (so that cancel
 *              callbacks never run inside a call from the application)
 *
 * Connections live in a fixed array, so a struct http_reply or
 * http_stream always points at valid memory; a serial number tells a
 * reply for an old connection from the one now using the slot.
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/log.h"
#include "http/http.h"
#include "http/internal.h"

#define DEFAULT_HEAD_TIMEOUT_MS 10000u
#define DEFAULT_IDLE_TIMEOUT_MS 30000u
#define DEFAULT_WRITE_TIMEOUT_MS 30000u
#define DEFAULT_KEEPALIVE_MS 20000u

/* Bytes read at a time. */
#define READ_CHUNK 4096

/* Received bytes kept: one whole request, and a read's worth more. */
#define INPUT_MAX (CAPS_HTTP_HEAD + CAPS_HTTP_BODY + READ_CHUNK)

/* Room for the extra headers of one response. */
#define EXTRA_MAX 4096

enum conn_state {
    FREE,
    READING,
    HANDLING,
    WRITING,
    STREAMING,
    CLOSING,
};

struct conn;

struct http_reply {
    struct conn *conn;
    bool active;
    http_cancel_fn *cancel;
    void *cancel_arg;
};

struct http_stream {
    struct conn *conn;
    bool active;
};

struct conn {
    struct http_server *server;
    enum conn_state state;
    uint64_t serial;
    plat_socket sock;
    struct plat_addr peer;
    bool peer_loopback;
    struct buf in;
    struct buf out;
    struct loop_timer timer;
    bool head_timer; /* the timer counts the head's time, not idle time */

    /* The request being handled. */
    struct http_request req;
    size_t head_len;
    bool is_head_method;
    bool close_after;

    struct http_reply reply;
    struct http_stream stream;
    struct buf extra; /* extra response headers, "Name: value\r\n" each */
    size_t extra_count;
    bool extra_csp; /* a Content-Security-Policy is among them */
};

struct http_server {
    struct loop *loop;
    struct http_server_options options;
    const struct http_router *router;
    plat_socket listener;
    struct plat_addr addr;
    bool loopback_only;
    uint64_t next_serial;
    size_t streams;
    struct conn conns[CAPS_HTTP_CONNECTIONS];
};

static const char *
reason(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 413:
        return "Content Too Large";
    case 415:
        return "Unsupported Media Type";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    case 505:
        return "HTTP Version Not Supported";
    default:
        return status < 400 ? "OK" : "Error";
    }
}

static void on_io(struct loop *loop, plat_socket sock, int events, void *arg);
static void process(struct conn *c);

static void
update_watch(struct conn *c)
{
    if (c->state == FREE || c->state == CLOSING) {
        return;
    }
    int events = LOOP_READ | (c->out.len > 0 ? LOOP_WRITE : 0);
    loop_watch(c->server->loop, c->sock, events, on_io, c);
}

static void
start_timer(struct conn *c, uint32_t ms, uint32_t repeat)
{
    loop_timer_start(c->server->loop, &c->timer, ms, repeat);
}

/*
 * Closes the connection and frees the slot.  If notify, and a reply or
 * stream was still open, its cancel callback runs last.
 */
static void
conn_close(struct conn *c, bool notify)
{
    if (c->state == FREE) {
        return;
    }
    struct http_server *s = c->server;
    http_cancel_fn *cancel = NULL;
    void *cancel_arg = NULL;
    if ((c->reply.active || c->stream.active) && notify) {
        cancel = c->reply.cancel;
        cancel_arg = c->reply.cancel_arg;
    }
    if (c->stream.active) {
        s->streams--;
    }
    loop_unwatch(s->loop, c->sock);
    plat_socket_close(c->sock);
    loop_timer_stop(s->loop, &c->timer);
    buf_fini(&c->in);
    buf_fini(&c->out);
    buf_fini(&c->extra);
    memset(&c->reply, 0, sizeof(c->reply));
    memset(&c->stream, 0, sizeof(c->stream));
    c->sock = PLAT_INVALID_SOCKET;
    c->state = FREE;
    if (cancel != NULL) {
        cancel(cancel_arg);
    }
}

/* Closes on the loop's next pass, then runs any cancel callback. */
static void
schedule_close(struct conn *c)
{
    if (c->state == FREE || c->state == CLOSING) {
        return;
    }
    loop_unwatch(c->server->loop, c->sock);
    c->state = CLOSING;
    start_timer(c, 0, 0);
}

/* Appends the status line and the headers every response carries. */
static int
append_head(struct conn *c, int status, const char *content_type)
{
    struct buf *o = &c->out;
    int err = buf_appendf(o, "HTTP/1.1 %d %s\r\n", status, reason(status));
    if (err == 0 && content_type != NULL) {
        err = buf_appendf(o, "Content-Type: %s\r\n", content_type);
    }
    if (err == 0) {
        err = buf_append_cstr(o, "Cache-Control: no-store\r\n"
                                 "X-Content-Type-Options: nosniff\r\n"
                                 "X-Frame-Options: DENY\r\n"
                                 "Referrer-Policy: no-referrer\r\n");
    }
    if (err == 0 && !c->extra_csp) {
        err = buf_append_cstr(o, "Content-Security-Policy: default-src "
                                 "'none'; frame-ancestors 'none'\r\n");
    }
    if (err == 0 && c->extra.len > 0) {
        err = buf_append_str(o, buf_str(&c->extra));
    }
    return err;
}

/*
 * Queues a whole response and moves to WRITING.  close_after is set by the
 * caller first, if the connection must close once it is sent.
 */
static int
respond(struct conn *c, int status, const char *content_type, const void *body,
        size_t len, const char *allow)
{
    size_t before = c->out.len;
    int err = append_head(c, status, content_type);
    if (err == 0 && allow != NULL && allow[0] != '\0') {
        err = buf_appendf(&c->out, "Allow: %s\r\n", allow);
    }
    if (err == 0 && status != 204) {
        /* %llu rather than %zu, which gcc on Windows does not accept. */
        err = buf_appendf(&c->out, "Content-Length: %llu\r\n",
                          (unsigned long long)len);
    }
    if (err == 0) {
        if (c->close_after) {
            err = buf_append_cstr(&c->out, "Connection: close\r\n");
        } else if (c->req.minor_version == 0) {
            err = buf_append_cstr(&c->out, "Connection: keep-alive\r\n");
        }
    }
    if (err == 0) {
        err = buf_append_cstr(&c->out, "\r\n");
    }
    if (err == 0 && status != 204 && !c->is_head_method && len > 0) {
        err = buf_append(&c->out, body, len);
    }
    if (err != 0) {
        /* Too big to send: say so instead, and close. */
        buf_truncate(&c->out, before);
        c->close_after = true;
        if (status != 500) {
            return respond(c, 500, "text/plain; charset=utf-8",
                           "response too large\n", 19, NULL);
        }
        schedule_close(c);
        return err;
    }
    c->state = WRITING;
    c->head_timer = false;
    start_timer(c, c->server->options.write_timeout_ms, 0);
    update_watch(c);
    return 0;
}

static void
refuse(struct conn *c, int status, const char *message, const char *allow)
{
    char body[256];
    int n = snprintf(body, sizeof(body), "%s\n", message);
    size_t len = n < 0                      ? 0
                 : (size_t)n < sizeof(body) ? (size_t)n
                                            : sizeof(body) - 1;
    respond(c, status, "text/plain; charset=utf-8", body, len, allow);
}

/* A response has been sent: the next request, or close. */
static void
finish_write(struct conn *c)
{
    if (c->close_after) {
        conn_close(c, false);
        return;
    }
    buf_consume(&c->in, c->head_len + c->req.content_length);
    c->head_len = 0;
    memset(&c->req, 0, sizeof(c->req));
    c->state = READING;
    c->head_timer = false;
    start_timer(c, c->server->options.idle_timeout_ms, 0);
    update_watch(c);
    /* The browser may already have sent its next request. */
    process(c);
}

static void
flush(struct conn *c)
{
    bool progress = false;
    while (c->out.len > 0) {
        size_t sent = 0;
        int err = plat_send(c->sock, c->out.data, c->out.len, &sent);
        if (err == ERR_AGAIN) {
            break;
        }
        if (err != 0) {
            conn_close(c, true);
            return;
        }
        buf_consume(&c->out, sent);
        progress = true;
    }
    if (c->out.len == 0 && c->state == WRITING) {
        finish_write(c);
        return;
    }
    if (progress && c->state == WRITING) {
        start_timer(c, c->server->options.write_timeout_ms, 0);
    }
    update_watch(c);
}

/* Handles whatever complete request is in the input. */
static void
process(struct conn *c)
{
    if (c->state != READING) {
        return;
    }
    struct http_server *s = c->server;

    if (c->in.len == 0) {
        return;
    }
    /*
     * The head is read again each time more arrives, rather than kept from
     * last time: the input buffer may have moved as it grew, and the
     * request's slices must point into it as it is now.
     */
    int status = 0;
    int err = http_parse_head(buf_str(&c->in), &c->req, &c->head_len, &status);
    if (err == ERR_AGAIN) {
        /* A request has begun: its head must arrive in time. */
        if (!c->head_timer) {
            c->head_timer = true;
            start_timer(c, s->options.head_timeout_ms, 0);
        }
        return;
    }
    c->close_after = true; /* until the request proves acceptable */
    if (err != 0) {
        refuse(c, status, reason(status), NULL);
        return;
    }
    c->is_head_method = str_eq(c->req.method, STR_LIT("HEAD"));
    c->req.peer = c->peer;
    c->req.peer_is_loopback = c->peer_loopback;
    if (!c->head_timer) {
        c->head_timer = true;
        start_timer(c, s->options.head_timeout_ms, 0);
    }

    char allow[64];
    const struct http_route *route = http_router_match(
        s->router, c->req.method, c->req.path, &status, allow, sizeof(allow));
    if (route == NULL) {
        refuse(c, status, status == 405 ? "method not allowed" : "not found",
               allow);
        return;
    }
    const char *message = NULL;
    status = http_guard(&c->req, s->loopback_only, route->local_only,
                        route->local_only_message, &message);
    if (status != 0) {
        refuse(c, status, message, NULL);
        return;
    }
    if (c->req.content_length > route->max_body) {
        refuse(c, 413, "request body too large", NULL);
        return;
    }
    if (c->in.len < c->head_len + c->req.content_length) {
        return; /* the body is still on its way */
    }
    c->req.body = str_make(c->in.data + c->head_len, c->req.content_length);
    c->close_after = !c->req.keep_alive;

    c->state = HANDLING;
    loop_timer_stop(s->loop, &c->timer);
    c->reply.conn = c;
    c->reply.active = true;
    c->reply.cancel = NULL;
    c->reply.cancel_arg = NULL;
    buf_clear(&c->extra);
    c->extra_count = 0;
    c->extra_csp = false;
    route->fn(&c->reply, &c->req, route->arg);
}

static void
read_some(struct conn *c)
{
    if (c->state == STREAMING) {
        /* Nothing is expected from a stream's browser: read and drop. */
        buf_clear(&c->in);
    }
    int err = buf_reserve(&c->in, READ_CHUNK);
    if (err != 0) {
        /* More pipelined data than one request can be: misbehaving. */
        conn_close(c, true);
        return;
    }
    size_t got = 0;
    err = plat_recv(c->sock, buf_tail(&c->in), READ_CHUNK, &got);
    if (err == ERR_AGAIN) {
        return;
    }
    if (err != 0) {
        conn_close(c, true);
        return;
    }
    buf_commit(&c->in, got);
    if (c->state == READING) {
        process(c);
    }
}

static void
on_io(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    struct conn *c = arg;

    if ((events & LOOP_WRITE) != 0) {
        flush(c);
        if (c->state == FREE) {
            return;
        }
    }
    if ((events & LOOP_READ) != 0) {
        read_some(c);
    }
}

static void
on_conn_timer(struct loop *loop, void *arg)
{
    (void)loop;
    struct conn *c = arg;

    switch (c->state) {
    case READING:
    case WRITING:
        /* Too slow to send a request, or to take a response. */
        conn_close(c, false);
        break;
    case STREAMING: {
        /* Keep the stream alive through proxies and idle timers. */
        if (c->out.len + 16 > CAPS_HTTP_SEND) {
            schedule_close(c);
            break;
        }
        buf_append_cstr(&c->out, ": keep-alive\n\n");
        update_watch(c);
        break;
    }
    case CLOSING:
        conn_close(c, true);
        break;
    case HANDLING:
    case FREE:
        break;
    }
}

static void
on_accept(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)events;
    struct http_server *s = arg;

    for (;;) {
        plat_socket client;
        struct plat_addr peer;
        int err = plat_tcp_accept(sock, &client, &peer);
        if (err == ERR_AGAIN) {
            return;
        }
        if (err != 0) {
            log_warn("web page: cannot accept a connection: %s", err_str(err));
            return;
        }
        struct conn *c = NULL;
        for (size_t i = 0; i < CAPS_HTTP_CONNECTIONS && c == NULL; i++) {
            if (s->conns[i].state == FREE) {
                c = &s->conns[i];
            }
        }
        if (c == NULL) {
            /* Too many at once: this one is turned away. */
            plat_socket_close(client);
            continue;
        }
        memset(c, 0, sizeof(*c));
        c->server = s;
        c->serial = s->next_serial++;
        c->sock = client;
        c->peer = peer;
        c->peer_loopback = plat_addr_is_loopback(&peer);
        buf_init(&c->in, INPUT_MAX);
        buf_init(&c->out, CAPS_HTTP_SEND);
        buf_init(&c->extra, EXTRA_MAX);
        loop_timer_init(&c->timer, on_conn_timer, c);
        c->state = READING;
        c->head_timer = true;
        start_timer(c, s->options.head_timeout_ms, 0);
        if (loop_watch(s->loop, client, LOOP_READ, on_io, c) != 0) {
            conn_close(c, false);
        }
    }
}

int
http_server_create(struct loop *loop,
                   const struct http_server_options *options,
                   const struct http_router *router, struct http_server **out)
{
    struct http_server *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return ERR_NOMEM;
    }
    s->loop = loop;
    s->options = *options;
    if (s->options.head_timeout_ms == 0) {
        s->options.head_timeout_ms = DEFAULT_HEAD_TIMEOUT_MS;
    }
    if (s->options.idle_timeout_ms == 0) {
        s->options.idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
    }
    if (s->options.write_timeout_ms == 0) {
        s->options.write_timeout_ms = DEFAULT_WRITE_TIMEOUT_MS;
    }
    if (s->options.keepalive_ms == 0) {
        s->options.keepalive_ms = DEFAULT_KEEPALIVE_MS;
    }
    s->router = router;
    s->listener = PLAT_INVALID_SOCKET;
    s->next_serial = 1;
    s->loopback_only = plat_addr_is_loopback(&options->listen);

    int err = plat_tcp_listen(&options->listen, &s->listener);
    if (err == 0) {
        err = plat_socket_local_addr(s->listener, &s->addr);
    }
    if (err == 0) {
        err = loop_watch(loop, s->listener, LOOP_READ, on_accept, s);
    }
    if (err != 0) {
        plat_socket_close(s->listener);
        free(s);
        return err;
    }
    *out = s;
    return 0;
}

void
http_server_destroy(struct http_server *s)
{
    if (s == NULL) {
        return;
    }
    for (size_t i = 0; i < CAPS_HTTP_CONNECTIONS; i++) {
        conn_close(&s->conns[i], true);
    }
    loop_unwatch(s->loop, s->listener);
    plat_socket_close(s->listener);
    free(s);
}

const struct plat_addr *
http_server_addr(const struct http_server *s)
{
    return &s->addr;
}

/* Replies. */

static bool
reply_usable(const struct http_reply *r)
{
    return r != NULL && r->active && r->conn != NULL &&
           r->conn->state == HANDLING;
}

void
http_reply_on_cancel(struct http_reply *r, http_cancel_fn *fn, void *arg)
{
    if (reply_usable(r)) {
        r->cancel = fn;
        r->cancel_arg = arg;
    }
}

/* A header name: letters, digits and '-'. */
static bool
is_header_name(const char *s)
{
    if (s[0] == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        char ch = *s;
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-'))
        {
            return false;
        }
    }
    return true;
}

int
http_reply_header(struct http_reply *r, const char *name, const char *value)
{
    if (!reply_usable(r)) {
        return ERR_CLOSED;
    }
    struct conn *c = r->conn;
    if (c->extra_count >= CAPS_HTTP_EXTRA_HEADERS) {
        return ERR_LIMIT;
    }
    /* No line breaks or controls: a value must not start a new header. */
    if (!is_header_name(name)) {
        return ERR_INVALID;
    }
    for (const char *v = value; *v != '\0'; v++) {
        unsigned char ch = (unsigned char)*v;
        if ((ch < 0x20 && ch != '\t') || ch == 0x7f) {
            return ERR_INVALID;
        }
    }
    int err = buf_appendf(&c->extra, "%s: %s\r\n", name, value);
    if (err != 0) {
        return err;
    }
    c->extra_count++;
    if (str_eq_nocase(str_from_cstr(name), STR_LIT("Content-Security-Policy")))
    {
        c->extra_csp = true;
    }
    return 0;
}

int
http_reply_send(struct http_reply *r, int status, const char *content_type,
                const void *body, size_t len)
{
    if (!reply_usable(r)) {
        return ERR_CLOSED;
    }
    struct conn *c = r->conn;
    r->active = false;
    return respond(c, status, content_type, body, len, NULL);
}

int
http_reply_error(struct http_reply *r, int status, const char *message)
{
    if (!reply_usable(r)) {
        return ERR_CLOSED;
    }
    char body[512];
    int n = snprintf(body, sizeof(body), "%s\n", message);
    size_t len = n < 0                      ? 0
                 : (size_t)n < sizeof(body) ? (size_t)n
                                            : sizeof(body) - 1;
    return http_reply_send(r, status, "text/plain; charset=utf-8", body, len);
}

/* Streams. */

int
http_reply_stream(struct http_reply *r, struct http_stream **out)
{
    if (!reply_usable(r)) {
        return ERR_CLOSED;
    }
    struct conn *c = r->conn;
    struct http_server *s = c->server;
    if (s->streams >= CAPS_HTTP_STREAMS) {
        c->close_after = true;
        http_reply_error(r, 503, "too many live-update streams open");
        return ERR_LIMIT;
    }
    int err = append_head(c, 200, "text/event-stream");
    if (err == 0) {
        err = buf_append_cstr(&c->out, "\r\n");
    }
    if (err != 0) {
        schedule_close(c);
        return err;
    }
    /* The reply becomes the stream; the request is done with. */
    buf_consume(&c->in, c->head_len + c->req.content_length);
    c->state = STREAMING;
    c->stream.conn = c;
    c->stream.active = true;
    r->active = false;
    s->streams++;
    start_timer(c, s->options.keepalive_ms, s->options.keepalive_ms);
    update_watch(c);
    *out = &c->stream;
    return 0;
}

int
http_stream_send(struct http_stream *st, struct str text)
{
    if (st == NULL || !st->active || st->conn->state != STREAMING) {
        return ERR_CLOSED;
    }
    struct conn *c = st->conn;
    size_t before = c->out.len;
    int err = 0;
    struct str rest = text;
    for (;;) {
        struct str line;
        struct str after;
        bool more = str_split(rest, '\n', &line, &after);
        if (!more) {
            line = rest;
        }
        /* A CR would end the line early too; drop it. */
        if (line.len > 0 && line.ptr[line.len - 1] == '\r') {
            line.len--;
        }
        err = buf_append_cstr(&c->out, "data: ");
        if (err == 0) {
            err = buf_append_str(&c->out, line);
        }
        if (err == 0) {
            err = buf_append_cstr(&c->out, "\n");
        }
        if (err != 0 || !more) {
            break;
        }
        rest = after;
    }
    if (err == 0) {
        err = buf_append_cstr(&c->out, "\n");
    }
    if (err != 0) {
        /* The browser is not keeping up: end the stream. */
        buf_truncate(&c->out, before);
        schedule_close(c);
        return ERR_LIMIT;
    }
    update_watch(c);
    return 0;
}

void
http_stream_close(struct http_stream *st)
{
    if (st == NULL || !st->active) {
        return;
    }
    conn_close(st->conn, false);
}
