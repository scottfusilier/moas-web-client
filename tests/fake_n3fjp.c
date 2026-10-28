/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "fake_n3fjp.h"

static void
close_client(struct fake_n3fjp *f)
{
    if (f->client != PLAT_INVALID_SOCKET) {
        loop_unwatch(f->loop, f->client);
        plat_socket_close(f->client);
        f->client = PLAT_INVALID_SOCKET;
    }
}

static void
fake_on_client(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    (void)events;
    struct fake_n3fjp *f = arg;
    char buf[256];
    size_t got = 0;
    int err = plat_recv(f->client, buf, sizeof(buf), &got);
    if (err == ERR_AGAIN) {
        return;
    }
    if (err != 0) {
        close_client(f);
        return;
    }
    for (size_t i = 0; i < got; i++) {
        if (buf[i] != '\n') {
            if (f->request_len < sizeof(f->request) - 1) {
                f->request[f->request_len++] = buf[i];
            }
            continue;
        }
        f->request[f->request_len] = '\0';
        f->request_len = 0;
        f->requests++;
        if (!f->silent) {
            fake_n3fjp_send(f, f->reply);
        }
    }
}

static void
fake_on_listener(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    struct fake_n3fjp *f = arg;
    plat_socket c;
    if (plat_tcp_accept(sock, &c, NULL) != 0) {
        return;
    }
    close_client(f);
    f->client = c;
    f->accepted++;
    f->request_len = 0;
    loop_watch(loop, c, LOOP_READ, fake_on_client, f);
}

void
fake_n3fjp_start(struct fake_n3fjp *f, struct loop *loop, const char *mhz)
{
    memset(f, 0, sizeof(*f));
    f->loop = loop;
    f->client = PLAT_INVALID_SOCKET;
    fake_n3fjp_set_freq(f, mhz);
    plat_addr_parse("127.0.0.1:0", 11, &f->addr);
    plat_tcp_listen(&f->addr, &f->listener);
    plat_socket_local_addr(f->listener, &f->addr);
    loop_watch(loop, f->listener, LOOP_READ, fake_on_listener, f);
}

void
fake_n3fjp_stop(struct fake_n3fjp *f)
{
    close_client(f);
    loop_unwatch(f->loop, f->listener);
    plat_socket_close(f->listener);
}

void
fake_n3fjp_set_freq(struct fake_n3fjp *f, const char *mhz)
{
    snprintf(f->reply, sizeof(f->reply),
             "<CMD><READBMFRESPONSE><BAND>20</BAND><MODE>CW</MODE>"
             "<MODETEST>CW</MODETEST><FREQ>%s</FREQ></CMD>\r\n",
             mhz);
}

void
fake_n3fjp_send(struct fake_n3fjp *f, const char *text)
{
    if (f->client != PLAT_INVALID_SOCKET) {
        size_t sent;
        plat_send(f->client, text, strlen(text), &sent);
    }
}

void
fake_n3fjp_hang_up(struct fake_n3fjp *f)
{
    fake_n3fjp_send(f, "\r\n\r\n");
    close_client(f);
}
