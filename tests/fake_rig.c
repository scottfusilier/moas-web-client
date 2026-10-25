/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "fake_rig.h"

static void
fake_on_client(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    struct fake_rig *f = arg;
    char buf[256];
    size_t got = 0;
    int err = plat_recv(sock, buf, sizeof(buf), &got);
    if (err == ERR_AGAIN) {
        return;
    }
    if (err != 0) {
        loop_unwatch(loop, sock);
        plat_socket_close(sock);
        f->client = PLAT_INVALID_SOCKET;
        return;
    }
    for (size_t i = 0; i < got; i++) {
        if (buf[i] == '\n' && !f->silent) {
            char line[80];
            snprintf(line, sizeof(line), "%s\n", f->reply);
            size_t sent;
            plat_send(sock, line, strlen(line), &sent);
        }
    }
}

static void
fake_on_listener(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    struct fake_rig *f = arg;
    plat_socket c;
    if (plat_tcp_accept(sock, &c, NULL) != 0) {
        return;
    }
    if (f->client != PLAT_INVALID_SOCKET) {
        loop_unwatch(loop, f->client);
        plat_socket_close(f->client);
    }
    f->client = c;
    f->accepted++;
    loop_watch(loop, c, LOOP_READ, fake_on_client, f);
}

void
fake_rig_start(struct fake_rig *f, struct loop *loop, const char *reply)
{
    memset(f, 0, sizeof(*f));
    f->loop = loop;
    f->client = PLAT_INVALID_SOCKET;
    snprintf(f->reply, sizeof(f->reply), "%s", reply);
    plat_addr_parse("127.0.0.1:0", 11, &f->addr);
    plat_tcp_listen(&f->addr, &f->listener);
    plat_socket_local_addr(f->listener, &f->addr);
    loop_watch(loop, f->listener, LOOP_READ, fake_on_listener, f);
}

void
fake_rig_stop(struct fake_rig *f)
{
    if (f->client != PLAT_INVALID_SOCKET) {
        loop_unwatch(f->loop, f->client);
        plat_socket_close(f->client);
    }
    loop_unwatch(f->loop, f->listener);
    plat_socket_close(f->listener);
}
