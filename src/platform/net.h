/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Sockets.  Every socket made here is non-blocking and is not inherited by
 * child processes.  Functions return 0 or an error code from
 * platform/err.h; ERR_AGAIN means "not now, wait until poll says the socket
 * is ready".
 *
 * This header does not include the system's socket headers, so including it
 * does not bring <windows.h> into the rest of the program.
 */

#ifndef MOAS_PLATFORM_NET_H
#define MOAS_PLATFORM_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
typedef uintptr_t plat_socket; /* the same size as Windows' SOCKET */
#define PLAT_INVALID_SOCKET (~(plat_socket)0)
#else
typedef int plat_socket;
#define PLAT_INVALID_SOCKET (-1)
#endif

/*
 * Starts and stops networking.  Call plat_net_init() once before any other
 * function here.  On Unix it also makes writing to a closed connection
 * return an error instead of stopping the program with SIGPIPE.
 */
int plat_net_init(void);
void plat_net_fini(void);

/* An IPv4 or IPv6 address and port. */
struct plat_addr {
    union {
        uint64_t align;
        unsigned char bytes[128]; /* a struct sockaddr_storage */
    } u;
    uint32_t len;
};

/* Longest text plat_addr_format() writes, including the terminating NUL. */
#define PLAT_ADDR_TEXT_MAX 80

/*
 * Parses "host:port".  host is an IPv4 address ("127.0.0.1"), an IPv6
 * address in brackets ("[::1]"), or "localhost" (127.0.0.1).  Host names
 * are not looked up.  port is 0 to 65535.  Returns ERR_ADDR if the text is
 * not such an address.
 */
int plat_addr_parse(const char *text, size_t len, struct plat_addr *out);

/* Writes the address as "host:port" (IPv6 in brackets). */
void plat_addr_format(const struct plat_addr *addr,
                      char out[PLAT_ADDR_TEXT_MAX]);

uint16_t plat_addr_port(const struct plat_addr *addr);

/* True for 127.0.0.0/8, ::1, and IPv4 loopback addresses written as IPv6. */
bool plat_addr_is_loopback(const struct plat_addr *addr);

/*
 * Starts connecting to addr.  The connection is usually still in progress
 * when this returns: wait until poll reports the socket writable (or an
 * error), then call plat_socket_error() to learn whether it worked.
 */
int plat_tcp_connect(const struct plat_addr *addr, plat_socket *out);

/*
 * Listens on addr.  On Windows no other program can take over the port
 * while it is in use; on Unix the port can be reused at once after
 * moas-web-client stops.
 */
int plat_tcp_listen(const struct plat_addr *addr, plat_socket *out);

/* Accepts a connection; ERR_AGAIN if none is waiting.  peer may be NULL. */
int plat_tcp_accept(plat_socket listener, plat_socket *out,
                    struct plat_addr *peer);

/* A UDP socket bound to addr, for receiving datagrams. */
int plat_udp_bind(const struct plat_addr *addr, plat_socket *out);

/* A UDP socket for sending datagrams, not bound to a port of its own. */
int plat_udp_open(const struct plat_addr *like, plat_socket *out);

/*
 * Sends up to len bytes.  *sent says how many went; it can be fewer than
 * len.  ERR_CLOSED if the other end has closed or reset the connection.
 */
int plat_send(plat_socket sock, const void *data, size_t len, size_t *sent);

/* Receives up to len bytes into data.  ERR_CLOSED at end of stream. */
int plat_recv(plat_socket sock, void *data, size_t len, size_t *got);

/*
 * Receives one datagram.  A datagram longer than len is cut short and
 * reported as ERR_LIMIT.  from may be NULL.
 */
int plat_recvfrom(plat_socket sock, void *data, size_t len, size_t *got,
                  struct plat_addr *from);

/* Sends one datagram to addr. */
int plat_sendto(plat_socket sock, const void *data, size_t len,
                const struct plat_addr *addr);

/*
 * The result of a connect started by plat_tcp_connect(): 0 once connected,
 * or the reason it failed, such as ERR_REFUSED.
 */
int plat_socket_error(plat_socket sock);

/* The local address a socket is bound to. */
int plat_socket_local_addr(plat_socket sock, struct plat_addr *out);

void plat_socket_close(plat_socket sock);

/*
 * Two connected TCP sockets on the loopback interface, both non-blocking:
 * what is sent on one can be received on the other.  Used to wake the event
 * loop, and by tests.
 */
int plat_socket_pair(plat_socket *a, plat_socket *b);

/* Flags for struct plat_pollfd. */
#define PLAT_POLLIN 0x1  /* readable, or a connection to accept */
#define PLAT_POLLOUT 0x2 /* writable, or a connect has finished */
#define PLAT_POLLERR 0x4 /* reported only: an error on the socket */
#define PLAT_POLLHUP 0x8 /* reported only: the other end hung up */

struct plat_pollfd {
    plat_socket sock;
    int events;  /* PLAT_POLLIN and/or PLAT_POLLOUT */
    int revents; /* set by plat_poll() */
};

/* The most sockets plat_poll() takes at once. */
#define PLAT_POLL_MAX 256

/*
 * Waits until at least one socket is ready or timeout_ms passes (-1 waits
 * without a limit).  *ready is the number of sockets with revents set.  n
 * must be 1 to PLAT_POLL_MAX.  A signal arriving during the wait ends it
 * early with *ready 0.
 */
int plat_poll(struct plat_pollfd *fds, size_t n, int timeout_ms,
              size_t *ready);

#endif /* MOAS_PLATFORM_NET_H */
