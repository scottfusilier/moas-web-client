/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A connected pair of sockets, built from the other platform functions so
 * that it is the same on every system.  (Unix has socketpair(), but Windows
 * can only poll sockets, and loopback TCP works everywhere.)
 */

#include "platform/err.h"
#include "platform/net.h"

/* How long to wait for the loopback connection; it is normally instant. */
#define PAIR_TIMEOUT_MS 5000

int
plat_socket_pair(plat_socket *a, plat_socket *b)
{
    struct plat_addr addr;
    plat_socket listener = PLAT_INVALID_SOCKET;
    plat_socket client = PLAT_INVALID_SOCKET;
    plat_socket server = PLAT_INVALID_SOCKET;

    int err = plat_addr_parse("127.0.0.1:0", 11, &addr);
    if (err == 0) {
        err = plat_tcp_listen(&addr, &listener);
    }
    if (err == 0) {
        err = plat_socket_local_addr(listener, &addr);
    }
    if (err == 0) {
        err = plat_tcp_connect(&addr, &client);
    }
    if (err == 0) {
        struct plat_pollfd p = {.sock = listener, .events = PLAT_POLLIN};
        size_t ready = 0;
        err = plat_poll(&p, 1, PAIR_TIMEOUT_MS, &ready);
        if (err == 0 && ready == 0) {
            err = ERR_TIMEOUT;
        }
    }
    struct plat_addr peer;
    struct plat_addr mine;
    if (err == 0) {
        err = plat_tcp_accept(listener, &server, &peer);
    }
    /*
     * Make sure the connection accepted is our own: another program could
     * have connected to the listening port first.
     */
    if (err == 0) {
        err = plat_socket_local_addr(client, &mine);
    }
    if (err == 0 && (plat_addr_port(&peer) != plat_addr_port(&mine) ||
                     !plat_addr_is_loopback(&peer)))
    {
        err = ERR_ACCESS;
    }
    if (err == 0) {
        struct plat_pollfd p = {.sock = client, .events = PLAT_POLLOUT};
        size_t ready = 0;
        err = plat_poll(&p, 1, PAIR_TIMEOUT_MS, &ready);
        if (err == 0) {
            err = ready == 0 ? ERR_TIMEOUT : plat_socket_error(client);
        }
    }

    plat_socket_close(listener);
    if (err != 0) {
        plat_socket_close(client);
        plat_socket_close(server);
        return err;
    }
    *a = client;
    *b = server;
    return 0;
}
