/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Address parsing and formatting, the same on every system apart from the
 * headers.
 */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "platform/err.h"
#include "platform/net.h"

/*
 * 127.0.0.1.  INADDR_LOOPBACK is not part of POSIX, and FreeBSD hides it
 * when only POSIX is asked for.
 */
#define LOOPBACK_V4 0x7f000001u

_Static_assert(sizeof(struct sockaddr_storage) <=
                   sizeof(((struct plat_addr *)0)->u.bytes),
               "struct plat_addr is too small for this system");

/* "localhost", ignoring ASCII case. */
static bool
is_localhost(const char *s, size_t len)
{
    static const char name[] = "localhost";

    if (len != sizeof(name) - 1) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != name[i]) {
            return false;
        }
    }
    return true;
}

/* 0 to 65535, digits only. */
static bool
parse_port(const char *s, size_t len, uint16_t *out)
{
    if (len == 0 || len > 5) {
        return false;
    }
    uint32_t port = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        port = port * 10u + (uint32_t)(s[i] - '0');
    }
    if (port > 65535u) {
        return false;
    }
    *out = (uint16_t)port;
    return true;
}

int
plat_addr_parse(const char *text, size_t len, struct plat_addr *out)
{
    const char *host;
    size_t host_len;
    const char *port_text;
    bool bracketed = false;

    if (len == 0 || memchr(text, '\0', len) != NULL) {
        return ERR_ADDR;
    }
    if (text[0] == '[') {
        const char *close = memchr(text, ']', len);
        if (close == NULL || (size_t)(close - text) + 1 >= len ||
            close[1] != ':')
        {
            return ERR_ADDR;
        }
        host = text + 1;
        host_len = (size_t)(close - host);
        port_text = close + 2;
        bracketed = true;
    } else {
        const char *colon = memchr(text, ':', len);
        if (colon == NULL) {
            return ERR_ADDR;
        }
        host = text;
        host_len = (size_t)(colon - text);
        port_text = colon + 1;
    }
    size_t port_len = len - (size_t)(port_text - text);

    uint16_t port;
    if (host_len == 0 || !parse_port(port_text, port_len, &port)) {
        return ERR_ADDR;
    }

    /* inet_pton() needs a NUL-terminated string. */
    char name[INET6_ADDRSTRLEN];
    if (host_len >= sizeof(name)) {
        return ERR_ADDR;
    }
    memcpy(name, host, host_len);
    name[host_len] = '\0';

    memset(out, 0, sizeof(*out));
    if (bracketed) {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        if (inet_pton(AF_INET6, name, &sin6.sin6_addr) != 1) {
            return ERR_ADDR;
        }
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(port);
        memcpy(out->u.bytes, &sin6, sizeof(sin6));
        out->len = (uint32_t)sizeof(sin6);
        return 0;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    if (is_localhost(host, host_len)) {
        sin.sin_addr.s_addr = htonl(LOOPBACK_V4);
    } else if (inet_pton(AF_INET, name, &sin.sin_addr) != 1) {
        return ERR_ADDR;
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    memcpy(out->u.bytes, &sin, sizeof(sin));
    out->len = (uint32_t)sizeof(sin);
    return 0;
}

static int
addr_family(const struct plat_addr *addr)
{
    struct sockaddr sa;

    if (addr->len < sizeof(sa)) {
        return AF_UNSPEC;
    }
    memcpy(&sa, addr->u.bytes, sizeof(sa));
    return sa.sa_family;
}

void
plat_addr_format(const struct plat_addr *addr, char out[PLAT_ADDR_TEXT_MAX])
{
    char host[INET6_ADDRSTRLEN];

    switch (addr_family(addr)) {
    case AF_INET: {
        struct sockaddr_in sin;
        memcpy(&sin, addr->u.bytes, sizeof(sin));
        if (inet_ntop(AF_INET, &sin.sin_addr, host, sizeof(host)) == NULL) {
            break;
        }
        snprintf(out, PLAT_ADDR_TEXT_MAX, "%s:%u", host,
                 (unsigned)ntohs(sin.sin_port));
        return;
    }
    case AF_INET6: {
        struct sockaddr_in6 sin6;
        memcpy(&sin6, addr->u.bytes, sizeof(sin6));
        if (inet_ntop(AF_INET6, &sin6.sin6_addr, host, sizeof(host)) == NULL) {
            break;
        }
        snprintf(out, PLAT_ADDR_TEXT_MAX, "[%s]:%u", host,
                 (unsigned)ntohs(sin6.sin6_port));
        return;
    }
    default:
        break;
    }
    snprintf(out, PLAT_ADDR_TEXT_MAX, "(no address)");
}

uint16_t
plat_addr_port(const struct plat_addr *addr)
{
    switch (addr_family(addr)) {
    case AF_INET: {
        struct sockaddr_in sin;
        memcpy(&sin, addr->u.bytes, sizeof(sin));
        return ntohs(sin.sin_port);
    }
    case AF_INET6: {
        struct sockaddr_in6 sin6;
        memcpy(&sin6, addr->u.bytes, sizeof(sin6));
        return ntohs(sin6.sin6_port);
    }
    default:
        return 0;
    }
}

bool
plat_addr_is_loopback(const struct plat_addr *addr)
{
    switch (addr_family(addr)) {
    case AF_INET: {
        struct sockaddr_in sin;
        memcpy(&sin, addr->u.bytes, sizeof(sin));
        return (ntohl(sin.sin_addr.s_addr) >> 24) == 127u;
    }
    case AF_INET6: {
        struct sockaddr_in6 sin6;
        memcpy(&sin6, addr->u.bytes, sizeof(sin6));
        const unsigned char *b = sin6.sin6_addr.s6_addr;
        static const unsigned char loopback[16] = {[15] = 1};
        static const unsigned char mapped[12] = {[10] = 0xff, [11] = 0xff};
        if (memcmp(b, loopback, sizeof(loopback)) == 0) {
            return true;
        }
        /* ::ffff:127.x.x.x, an IPv4 loopback address written as IPv6. */
        return memcmp(b, mapped, sizeof(mapped)) == 0 && b[12] == 127;
    }
    default:
        return false;
    }
}
