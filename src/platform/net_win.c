/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Sockets on Windows (Winsock 2.2, Windows 10 or later). */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <limits.h>
#include <string.h>

#include "platform/err.h"
#include "platform/net.h"

_Static_assert(sizeof(SOCKET) == sizeof(plat_socket),
               "plat_socket must hold a SOCKET");

#define LISTEN_BACKLOG 64

/* Not in every SDK's headers: stop ICMP "port unreachable" resetting UDP. */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

static int
from_wsa(int e)
{
    err_set_os_code(e);
    switch (e) {
    case WSAEWOULDBLOCK:
    case WSAEINPROGRESS:
        return ERR_AGAIN;
    case WSAECONNREFUSED:
        return ERR_REFUSED;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENOTCONN:
    case WSAESHUTDOWN:
        return ERR_CLOSED;
    case WSAETIMEDOUT:
        return ERR_TIMEOUT;
    case WSAEADDRINUSE:
        return ERR_ADDR_IN_USE;
    case WSAEADDRNOTAVAIL:
    case WSAEAFNOSUPPORT:
        return ERR_ADDR;
    case WSAEACCES:
        return ERR_ACCESS;
    case WSAENOBUFS:
        return ERR_NOMEM;
    case WSAEMSGSIZE:
        return ERR_LIMIT;
    default:
        return ERR_IO;
    }
}

static int
last_error(void)
{
    return from_wsa(WSAGetLastError());
}

int
plat_net_init(void)
{
    WSADATA wsa;

    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        return from_wsa(rc);
    }
    if (LOBYTE(wsa.wVersion) != 2 || HIBYTE(wsa.wVersion) != 2) {
        WSACleanup();
        return ERR_IO;
    }
    return 0;
}

void
plat_net_fini(void)
{
    WSACleanup();
}

static int
family_of(const struct plat_addr *addr)
{
    struct sockaddr sa;

    memcpy(&sa, addr->u.bytes, sizeof(sa));
    return sa.sa_family;
}

static const struct sockaddr *
sockaddr_of(const struct plat_addr *addr)
{
    return (const struct sockaddr *)(const void *)addr->u.bytes;
}

static int
set_nonblocking(SOCKET s)
{
    u_long on = 1;

    if (ioctlsocket(s, (long)FIONBIO, &on) != 0) {
        return last_error();
    }
    return 0;
}

static int
open_socket(const struct plat_addr *addr, int type, SOCKET *out)
{
    if (addr->len < sizeof(struct sockaddr)) {
        return ERR_ADDR;
    }
    SOCKET s = WSASocketW(family_of(addr), type, 0, NULL, 0,
                          WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) {
        return last_error();
    }
    int err = set_nonblocking(s);
    if (err != 0) {
        closesocket(s);
        return err;
    }
    *out = s;
    return 0;
}

int
plat_tcp_connect(const struct plat_addr *addr, plat_socket *out)
{
    SOCKET s;
    int err = open_socket(addr, SOCK_STREAM, &s);
    if (err != 0) {
        return err;
    }
    if (connect(s, sockaddr_of(addr), (int)addr->len) != 0 &&
        WSAGetLastError() != WSAEWOULDBLOCK)
    {
        err = last_error();
        closesocket(s);
        return err;
    }
    *out = (plat_socket)s;
    return 0;
}

int
plat_tcp_listen(const struct plat_addr *addr, plat_socket *out)
{
    SOCKET s;
    int err = open_socket(addr, SOCK_STREAM, &s);
    if (err != 0) {
        return err;
    }
    /*
     * On Windows SO_REUSEADDR would let another program bind the same port
     * and take connections meant for us; SO_EXCLUSIVEADDRUSE prevents that.
     */
    BOOL on = TRUE;
    if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&on,
                   (int)sizeof(on)) != 0 ||
        bind(s, sockaddr_of(addr), (int)addr->len) != 0 ||
        listen(s, LISTEN_BACKLOG) != 0)
    {
        err = last_error();
        closesocket(s);
        return err;
    }
    *out = (plat_socket)s;
    return 0;
}

int
plat_tcp_accept(plat_socket listener, plat_socket *out, struct plat_addr *peer)
{
    struct plat_addr from;
    int len = (int)sizeof(from.u.bytes);

    memset(&from, 0, sizeof(from));
    SOCKET s = accept((SOCKET)listener,
                      (struct sockaddr *)(void *)from.u.bytes, &len);
    if (s == INVALID_SOCKET) {
        if (WSAGetLastError() == WSAECONNRESET) {
            /* A client that gave up before being accepted. */
            return ERR_AGAIN;
        }
        return last_error();
    }
    /*
     * An accepted socket takes its listener's properties, but set
     * non-blocking mode and no inheritance explicitly rather than rely on
     * that.
     */
    int err = set_nonblocking(s);
    if (err == 0 && !SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0)) {
        err = ERR_IO;
        err_set_os_code((int)GetLastError());
    }
    if (err != 0) {
        closesocket(s);
        return err;
    }
    from.len = (uint32_t)len;
    if (peer != NULL) {
        *peer = from;
    }
    *out = (plat_socket)s;
    return 0;
}

static int
open_udp(const struct plat_addr *addr, SOCKET *out)
{
    SOCKET s;
    int err = open_socket(addr, SOCK_DGRAM, &s);
    if (err != 0) {
        return err;
    }
    BOOL off = FALSE;
    DWORD unused = 0;
    if (WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &unused,
                 NULL, NULL) != 0)
    {
        err = last_error();
        closesocket(s);
        return err;
    }
    *out = s;
    return 0;
}

int
plat_udp_bind(const struct plat_addr *addr, plat_socket *out)
{
    SOCKET s;
    int err = open_udp(addr, &s);
    if (err != 0) {
        return err;
    }
    if (bind(s, sockaddr_of(addr), (int)addr->len) != 0) {
        err = last_error();
        closesocket(s);
        return err;
    }
    *out = (plat_socket)s;
    return 0;
}

int
plat_udp_open(const struct plat_addr *like, plat_socket *out)
{
    SOCKET s;
    int err = open_udp(like, &s);
    if (err != 0) {
        return err;
    }
    *out = (plat_socket)s;
    return 0;
}

/* Winsock takes int lengths; larger requests are split by the caller. */
static int
clamp_len(size_t len)
{
    return len > (size_t)INT_MAX ? INT_MAX : (int)len;
}

int
plat_send(plat_socket sock, const void *data, size_t len, size_t *sent)
{
    int n = send((SOCKET)sock, data, clamp_len(len), 0);
    if (n == SOCKET_ERROR) {
        return last_error();
    }
    *sent = (size_t)n;
    return 0;
}

int
plat_recv(plat_socket sock, void *data, size_t len, size_t *got)
{
    int n = recv((SOCKET)sock, data, clamp_len(len), 0);
    if (n == SOCKET_ERROR) {
        return last_error();
    }
    if (n == 0 && len > 0) {
        return ERR_CLOSED;
    }
    *got = (size_t)n;
    return 0;
}

int
plat_recvfrom(plat_socket sock, void *data, size_t len, size_t *got,
              struct plat_addr *from)
{
    struct plat_addr addr;
    int addr_len = (int)sizeof(addr.u.bytes);

    memset(&addr, 0, sizeof(addr));
    int n = recvfrom((SOCKET)sock, data, clamp_len(len), 0,
                     (struct sockaddr *)(void *)addr.u.bytes, &addr_len);
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEMSGSIZE) {
            /* The datagram was cut short to fit; the buffer is full. */
            *got = len;
            err_set_os_code(e);
            return ERR_LIMIT;
        }
        return from_wsa(e);
    }
    addr.len = (uint32_t)addr_len;
    if (from != NULL) {
        *from = addr;
    }
    *got = (size_t)n;
    return 0;
}

int
plat_sendto(plat_socket sock, const void *data, size_t len,
            const struct plat_addr *addr)
{
    if (len > (size_t)INT_MAX) {
        return ERR_LIMIT;
    }
    int n = sendto((SOCKET)sock, data, (int)len, 0, sockaddr_of(addr),
                   (int)addr->len);
    if (n == SOCKET_ERROR) {
        return last_error();
    }
    return (size_t)n == len ? 0 : ERR_IO;
}

int
plat_socket_error(plat_socket sock)
{
    int e = 0;
    int len = (int)sizeof(e);

    if (getsockopt((SOCKET)sock, SOL_SOCKET, SO_ERROR, (char *)&e, &len) != 0)
    {
        return last_error();
    }
    return e == 0 ? 0 : from_wsa(e);
}

int
plat_socket_local_addr(plat_socket sock, struct plat_addr *out)
{
    int len = (int)sizeof(out->u.bytes);

    memset(out, 0, sizeof(*out));
    if (getsockname((SOCKET)sock, (struct sockaddr *)(void *)out->u.bytes,
                    &len) != 0)
    {
        return last_error();
    }
    out->len = (uint32_t)len;
    return 0;
}

void
plat_socket_close(plat_socket sock)
{
    if (sock != PLAT_INVALID_SOCKET) {
        closesocket((SOCKET)sock);
    }
}

int
plat_poll(struct plat_pollfd *fds, size_t n, int timeout_ms, size_t *ready)
{
    WSAPOLLFD native[PLAT_POLL_MAX];

    if (n == 0 || n > PLAT_POLL_MAX) {
        return ERR_INVALID;
    }
    for (size_t i = 0; i < n; i++) {
        native[i].fd = (SOCKET)fds[i].sock;
        /* WSAPoll() rejects anything but these in events. */
        native[i].events =
            (SHORT)(((fds[i].events & PLAT_POLLIN) != 0 ? POLLRDNORM : 0) |
                    ((fds[i].events & PLAT_POLLOUT) != 0 ? POLLWRNORM : 0));
        native[i].revents = 0;
    }
    int rc = WSAPoll(native, (ULONG)n, timeout_ms);
    if (rc == SOCKET_ERROR) {
        return last_error();
    }
    for (size_t i = 0; i < n; i++) {
        int r = native[i].revents;
        fds[i].revents = ((r & (POLLRDNORM | POLLRDBAND)) != 0 ? PLAT_POLLIN
                                                               : 0) |
                         ((r & POLLWRNORM) != 0 ? PLAT_POLLOUT : 0) |
                         ((r & (POLLERR | POLLNVAL)) != 0 ? PLAT_POLLERR : 0) |
                         ((r & POLLHUP) != 0 ? PLAT_POLLHUP : 0);
    }
    *ready = (size_t)rc;
    return 0;
}
