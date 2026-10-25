/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/json.h"
#include "fake_moas_server.h"
#include "oasp/frame.h"
#include "platform/clock.h"

struct fake_server {
    struct loop *loop;
    struct plat_addr addr;
    plat_socket listener;
    plat_socket client;
    int accepted;
    struct buf received;
    fake_connect_fn *on_connect;
    void *on_connect_arg;
    fake_message_fn *on_message;
    void *on_message_arg;
    size_t parsed; /* bytes of received already handed to on_message */
    struct oasp_framer framer;
};

static void
drop(struct fake_server *s)
{
    if (s->client != PLAT_INVALID_SOCKET) {
        loop_unwatch(s->loop, s->client);
        plat_socket_close(s->client);
        s->client = PLAT_INVALID_SOCKET;
    }
}

static void
on_client(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)events;
    struct fake_server *s = arg;
    char data[4096];
    size_t got = 0;

    int err = plat_recv(sock, data, sizeof(data), &got);
    if (err == ERR_AGAIN) {
        return;
    }
    if (err != 0) {
        drop(s);
        return;
    }
    buf_append(&s->received, data, got);

    /* Hand each complete message to the test, if it asked. */
    while (s->on_message != NULL && s->client != PLAT_INVALID_SOCKET) {
        size_t start;
        size_t end;
        const char *base = s->received.data + s->parsed;
        if (oasp_frame_next(&s->framer, base, s->received.len - s->parsed,
                            &start, &end) != 0)
        {
            break;
        }
        cJSON *msg = NULL;
        if (json_parse(str_make(base + start, end - start), &msg) == 0) {
            s->on_message(s, msg, s->on_message_arg);
            cJSON_Delete(msg);
        }
        s->parsed += end;
        oasp_framer_reset(&s->framer);
    }
}

static void
on_listener(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    struct fake_server *s = arg;
    plat_socket client;

    if (plat_tcp_accept(sock, &client, NULL) != 0) {
        return;
    }
    drop(s);
    s->client = client;
    s->accepted++;
    buf_clear(&s->received);
    s->parsed = 0;
    oasp_framer_reset(&s->framer);
    loop_watch(loop, client, LOOP_READ, on_client, s);
    if (s->on_connect != NULL) {
        s->on_connect(s, s->on_connect_arg);
    }
}

int
fake_server_create(struct loop *loop, struct fake_server **out)
{
    struct fake_server *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return ERR_NOMEM;
    }
    s->loop = loop;
    s->client = PLAT_INVALID_SOCKET;
    buf_init(&s->received, 1024 * 1024);
    int err = plat_addr_parse("127.0.0.1:0", 11, &s->addr);
    if (err == 0) {
        err = plat_tcp_listen(&s->addr, &s->listener);
    }
    if (err == 0) {
        err = plat_socket_local_addr(s->listener, &s->addr);
    }
    if (err == 0) {
        err = loop_watch(loop, s->listener, LOOP_READ, on_listener, s);
    }
    if (err != 0) {
        free(s);
        return err;
    }
    *out = s;
    return 0;
}

void
fake_server_destroy(struct fake_server *s)
{
    if (s == NULL) {
        return;
    }
    drop(s);
    loop_unwatch(s->loop, s->listener);
    plat_socket_close(s->listener);
    buf_fini(&s->received);
    free(s);
}

void
fake_server_on_connect(struct fake_server *s, fake_connect_fn *fn, void *arg)
{
    s->on_connect = fn;
    s->on_connect_arg = arg;
}

void
fake_server_on_message(struct fake_server *s, fake_message_fn *fn, void *arg)
{
    s->on_message = fn;
    s->on_message_arg = arg;
}

const struct plat_addr *
fake_server_addr(const struct fake_server *s)
{
    return &s->addr;
}

int
fake_server_accepted(const struct fake_server *s)
{
    return s->accepted;
}

bool
fake_server_has_client(const struct fake_server *s)
{
    return s->client != PLAT_INVALID_SOCKET;
}

struct str
fake_server_received(const struct fake_server *s)
{
    return buf_str(&s->received);
}

void
fake_server_send(struct fake_server *s, const char *data, size_t len)
{
    while (len > 0 && s->client != PLAT_INVALID_SOCKET) {
        size_t sent = 0;
        int err = plat_send(s->client, data, len, &sent);
        if (err == ERR_AGAIN) {
            plat_sleep_ms(1);
            continue;
        }
        if (err != 0) {
            drop(s);
            return;
        }
        data += sent;
        len -= sent;
    }
}

void
fake_server_send_cstr(struct fake_server *s, const char *text)
{
    fake_server_send(s, text, strlen(text));
}

void
fake_server_drop_client(struct fake_server *s)
{
    drop(s);
}

bool
run_until(struct loop *loop, bool (*done)(void *arg), void *arg,
          int timeout_ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)timeout_ms;
    while (!done(arg) && plat_monotonic_ms() < end) {
        loop_run_once(loop, 20);
    }
    return done(arg);
}
