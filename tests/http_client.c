/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_client.h"
#include "platform/clock.h"

#define WAIT_MS 5000

void
client_pump(struct client *c)
{
    loop_run_once(c->loop, 5);
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

bool
client_open(struct client *c, struct loop *loop, const struct plat_addr *addr)
{
    memset(c, 0, sizeof(*c));
    c->loop = loop;
    c->sock = PLAT_INVALID_SOCKET;
    buf_init(&c->in, 64 * 1024 * 1024);
    if (plat_tcp_connect(addr, &c->sock) != 0) {
        return false;
    }
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
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

void
client_close(struct client *c)
{
    plat_socket_close(c->sock);
    c->sock = PLAT_INVALID_SOCKET;
    buf_fini(&c->in);
}

void
client_send(struct client *c, const char *text)
{
    size_t len = strlen(text);
    while (len > 0) {
        size_t sent = 0;
        int err = plat_send(c->sock, text, len, &sent);
        if (err == ERR_AGAIN) {
            loop_run_once(c->loop, 5);
            continue;
        }
        if (err != 0) {
            return;
        }
        text += sent;
        len -= sent;
    }
}

void
response_fini(struct response *r)
{
    free(r->body);
    r->body = NULL;
}

bool
client_response(struct client *c, struct response *r, bool expect_body)
{
    memset(r, 0, sizeof(*r));
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
    while (plat_monotonic_ms() < end) {
        client_pump(c);
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
        r->body = malloc(body_len + 1);
        if (r->body != NULL) {
            memcpy(r->body, data + head_len, body_len);
            r->body[body_len] = '\0';
        }
        r->body_len = body_len;
        buf_consume(&c->in, head_len + body_len);
        return r->body != NULL;
    }
    return false;
}

bool
client_event(struct client *c, struct buf *out)
{
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
    buf_clear(out);
    while (plat_monotonic_ms() < end) {
        client_pump(c);
        const char *data = buf_cstr(&c->in);
        const char *blank = strstr(data, "\n\n");
        if (blank != NULL) {
            size_t n = (size_t)(blank - data);
            buf_append(out, data, n);
            buf_consume(&c->in, n + 2);
            return true;
        }
        if (c->closed) {
            return false;
        }
    }
    return false;
}

bool
client_sees_close(struct client *c, int timeout_ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)timeout_ms;
    while (!c->closed && plat_monotonic_ms() < end) {
        client_pump(c);
    }
    return c->closed;
}

int
http_raw(struct loop *loop, const struct plat_addr *addr, const char *request,
         bool expect_body, struct response *r)
{
    struct client c;
    int status = -1;
    memset(r, 0, sizeof(*r));
    if (client_open(&c, loop, addr)) {
        client_send(&c, request);
        if (client_response(&c, r, expect_body)) {
            status = r->status;
        }
    }
    client_close(&c);
    return status;
}

int
http_request(struct loop *loop, const struct plat_addr *addr,
             const char *method, const char *path, const char *body,
             const char *extra_headers, struct response *r)
{
    struct buf req;
    buf_init(&req, 1024 * 1024);
    buf_appendf(&req, "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\n", method, path);
    if (body != NULL) {
        buf_appendf(&req,
                    "Content-Type: application/json\r\n"
                    "Content-Length: %llu\r\n",
                    (unsigned long long)strlen(body));
    }
    if (extra_headers != NULL) {
        buf_append_cstr(&req, extra_headers);
    }
    buf_append_cstr(&req, "\r\n");
    if (body != NULL) {
        buf_append_cstr(&req, body);
    }
    int status = http_raw(loop, addr, buf_cstr(&req),
                          strcmp(method, "HEAD") != 0, r);
    buf_fini(&req);
    return status;
}
