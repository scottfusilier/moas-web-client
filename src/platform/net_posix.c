/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Sockets on FreeBSD, Linux and macOS.  Only interfaces all three share are
 * used (see docs/ROADMAP.md): fcntl() rather than SOCK_NONBLOCK or
 * accept4(), and SIGPIPE ignored rather than MSG_NOSIGNAL.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include "platform/err.h"
#include "platform/net.h"

#define LISTEN_BACKLOG 64

static int
from_errno(int e)
{
    err_set_os_code(e);
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS) {
        return ERR_AGAIN;
    }
    if (e == ECONNREFUSED) {
        return ERR_REFUSED;
    }
    if (e == ECONNRESET || e == EPIPE || e == ECONNABORTED || e == ENOTCONN) {
        return ERR_CLOSED;
    }
    if (e == ETIMEDOUT) {
        return ERR_TIMEOUT;
    }
    if (e == EADDRINUSE) {
        return ERR_ADDR_IN_USE;
    }
    if (e == EADDRNOTAVAIL || e == EAFNOSUPPORT) {
        return ERR_ADDR;
    }
    if (e == EACCES || e == EPERM) {
        return ERR_ACCESS;
    }
    if (e == ENOMEM || e == ENOBUFS) {
        return ERR_NOMEM;
    }
    if (e == EMSGSIZE) {
        return ERR_LIMIT;
    }
    return ERR_IO;
}

int
plat_net_init(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        return from_errno(errno);
    }
    return 0;
}

void
plat_net_fini(void)
{
}

/* Non-blocking, closed on exec, and no SIGPIPE where the system allows. */
static int
prepare(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return from_errno(errno);
    }
    flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return from_errno(errno);
    }
#ifdef SO_NOSIGPIPE
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) != 0) {
        return from_errno(errno);
    }
#endif
    return 0;
}

static int
family_of(const struct plat_addr *addr)
{
    struct sockaddr sa;

    memcpy(&sa, addr->u.bytes, sizeof(sa));
    return sa.sa_family;
}

static int
open_socket(const struct plat_addr *addr, int type, int *out)
{
    if (addr->len < sizeof(struct sockaddr)) {
        return ERR_ADDR;
    }
    int fd = socket(family_of(addr), type, 0);
    if (fd == -1) {
        return from_errno(errno);
    }
    int err = prepare(fd);
    if (err != 0) {
        close(fd);
        return err;
    }
    *out = fd;
    return 0;
}

static const struct sockaddr *
sockaddr_of(const struct plat_addr *addr)
{
    return (const struct sockaddr *)(const void *)addr->u.bytes;
}

int
plat_tcp_connect(const struct plat_addr *addr, plat_socket *out)
{
    int fd;
    int err = open_socket(addr, SOCK_STREAM, &fd);
    if (err != 0) {
        return err;
    }
    if (connect(fd, sockaddr_of(addr), (socklen_t)addr->len) != 0 &&
        errno != EINPROGRESS && errno != EINTR)
    {
        /* EINTR: the connection carries on in the background. */
        err = from_errno(errno);
        close(fd);
        return err;
    }
    *out = fd;
    return 0;
}

int
plat_tcp_listen(const struct plat_addr *addr, plat_socket *out)
{
    int fd;
    int err = open_socket(addr, SOCK_STREAM, &fd);
    if (err != 0) {
        return err;
    }
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0 ||
        bind(fd, sockaddr_of(addr), (socklen_t)addr->len) != 0 ||
        listen(fd, LISTEN_BACKLOG) != 0)
    {
        err = from_errno(errno);
        close(fd);
        return err;
    }
    *out = fd;
    return 0;
}

int
plat_tcp_accept(plat_socket listener, plat_socket *out, struct plat_addr *peer)
{
    struct plat_addr from;
    socklen_t len = (socklen_t)sizeof(from.u.bytes);

    memset(&from, 0, sizeof(from));
    int fd;
    do {
        fd = accept(listener, (struct sockaddr *)(void *)from.u.bytes, &len);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        /* A client that gave up before being accepted: nothing to do. */
        if (errno == ECONNABORTED) {
            return ERR_AGAIN;
        }
        return from_errno(errno);
    }
    int err = prepare(fd);
    if (err != 0) {
        close(fd);
        return err;
    }
    from.len = (uint32_t)len;
    if (peer != NULL) {
        *peer = from;
    }
    *out = fd;
    return 0;
}

int
plat_udp_bind(const struct plat_addr *addr, plat_socket *out)
{
    int fd;
    int err = open_socket(addr, SOCK_DGRAM, &fd);
    if (err != 0) {
        return err;
    }
    if (bind(fd, sockaddr_of(addr), (socklen_t)addr->len) != 0) {
        err = from_errno(errno);
        close(fd);
        return err;
    }
    *out = fd;
    return 0;
}

int
plat_udp_open(const struct plat_addr *like, plat_socket *out)
{
    return open_socket(like, SOCK_DGRAM, out);
}

int
plat_send(plat_socket sock, const void *data, size_t len, size_t *sent)
{
    ssize_t n;

    do {
        n = send(sock, data, len, 0);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        return from_errno(errno);
    }
    *sent = (size_t)n;
    return 0;
}

int
plat_recv(plat_socket sock, void *data, size_t len, size_t *got)
{
    ssize_t n;

    do {
        n = recv(sock, data, len, 0);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        return from_errno(errno);
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
    struct iovec iov = {.iov_base = data, .iov_len = len};
    struct msghdr msg;

    memset(&addr, 0, sizeof(addr));
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = addr.u.bytes;
    msg.msg_namelen = (socklen_t)sizeof(addr.u.bytes);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ssize_t n;
    do {
        n = recvmsg(sock, &msg, 0);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        return from_errno(errno);
    }
    addr.len = (uint32_t)msg.msg_namelen;
    if (from != NULL) {
        *from = addr;
    }
    *got = (size_t)n;
    if ((msg.msg_flags & MSG_TRUNC) != 0) {
        return ERR_LIMIT;
    }
    return 0;
}

int
plat_sendto(plat_socket sock, const void *data, size_t len,
            const struct plat_addr *addr)
{
    ssize_t n;

    do {
        n = sendto(sock, data, len, 0, sockaddr_of(addr),
                   (socklen_t)addr->len);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        return from_errno(errno);
    }
    return (size_t)n == len ? 0 : ERR_IO;
}

int
plat_socket_error(plat_socket sock)
{
    int e = 0;
    socklen_t len = (socklen_t)sizeof(e);

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &e, &len) != 0) {
        return from_errno(errno);
    }
    return e == 0 ? 0 : from_errno(e);
}

int
plat_socket_local_addr(plat_socket sock, struct plat_addr *out)
{
    socklen_t len = (socklen_t)sizeof(out->u.bytes);

    memset(out, 0, sizeof(*out));
    if (getsockname(sock, (struct sockaddr *)(void *)out->u.bytes, &len) != 0)
    {
        return from_errno(errno);
    }
    out->len = (uint32_t)len;
    return 0;
}

void
plat_socket_close(plat_socket sock)
{
    if (sock != PLAT_INVALID_SOCKET) {
        /* After EINTR the descriptor is gone on these systems; never retry. */
        close(sock);
    }
}

int
plat_poll(struct plat_pollfd *fds, size_t n, int timeout_ms, size_t *ready)
{
    struct pollfd native[PLAT_POLL_MAX];

    if (n == 0 || n > PLAT_POLL_MAX) {
        return ERR_INVALID;
    }
    for (size_t i = 0; i < n; i++) {
        native[i].fd = fds[i].sock;
        native[i].events =
            (short)(((fds[i].events & PLAT_POLLIN) != 0 ? POLLIN : 0) |
                    ((fds[i].events & PLAT_POLLOUT) != 0 ? POLLOUT : 0));
        native[i].revents = 0;
    }
    int rc = poll(native, (nfds_t)n, timeout_ms);
    if (rc == -1) {
        if (errno == EINTR) {
            for (size_t i = 0; i < n; i++) {
                fds[i].revents = 0;
            }
            *ready = 0;
            return 0;
        }
        return from_errno(errno);
    }
    for (size_t i = 0; i < n; i++) {
        int r = native[i].revents;
        fds[i].revents = ((r & POLLIN) != 0 ? PLAT_POLLIN : 0) |
                         ((r & POLLOUT) != 0 ? PLAT_POLLOUT : 0) |
                         ((r & (POLLERR | POLLNVAL)) != 0 ? PLAT_POLLERR : 0) |
                         ((r & POLLHUP) != 0 ? PLAT_POLLHUP : 0);
    }
    *ready = (size_t)rc;
    return 0;
}
