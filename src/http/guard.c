/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The rules every request must meet before a handler sees it.  The MOAS
 * protocol has no login, so anything that can send requests here can switch
 * antennas; these rules make sure requests come from moasweb's own page.
 * They are the Go version's rules.
 */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "http/http.h"

/* The Host header names this computer: localhost, 127.x.x.x or [::1]. */
static bool
is_loopback_host(struct str host)
{
    host = str_trim(host);
    /* Without the port.  "[::1]:8080" keeps its brackets for parsing. */
    struct str name = host;
    if (host.len > 0 && host.ptr[0] == '[') {
        const char *close = memchr(host.ptr, ']', host.len);
        if (close == NULL) {
            return false;
        }
        name = str_make(host.ptr, (size_t)(close - host.ptr) + 1);
    } else {
        struct str port;
        str_split(host, ':', &name, &port);
    }
    if (str_eq_nocase(name, STR_LIT("localhost"))) {
        return true;
    }
    /* plat_addr_parse() wants a port; any will do. */
    char text[64];
    if (name.len == 0 || name.len > 48) {
        return false;
    }
    snprintf(text, sizeof(text), "%.*s:0", (int)name.len, name.ptr);
    struct plat_addr addr;
    return plat_addr_parse(text, strlen(text), &addr) == 0 &&
           plat_addr_is_loopback(&addr);
}

/* The media type, without parameters: "application/json; charset=x". */
static struct str
media_type(struct str content_type)
{
    struct str type = content_type;
    struct str params;
    str_split(content_type, ';', &type, &params);
    return str_trim(type);
}

int
http_guard(const struct http_request *req, bool loopback_only, bool local_only,
           const char *local_only_message, const char **message)
{
    struct str host = http_header_value(req, "Host");

    if (loopback_only && !is_loopback_host(host)) {
        *message = "this page is only served to the computer it runs on";
        return 403;
    }
    if (str_eq(req->method, STR_LIT("POST"))) {
        /*
         * A browser says which site a request comes from.  It must be this
         * page itself.
         */
        struct str origin = http_header_value(req, "Origin");
        if (origin.ptr != NULL) {
            bool same = str_starts_with(origin, STR_LIT("http://")) &&
                        str_eq_nocase(str_make(origin.ptr + 7, origin.len - 7),
                                      str_trim(host));
            if (!same) {
                *message = "request from another site refused";
                return 403;
            }
        }
        /*
         * A page on another site cannot send this content type without
         * permission this server never gives.
         */
        struct str type = media_type(http_header_value(req, "Content-Type"));
        if (!str_eq_nocase(type, STR_LIT("application/json"))) {
            *message = "Content-Type must be application/json";
            return 415;
        }
    }
    if (local_only && !req->peer_is_loopback) {
        *message = local_only_message != NULL
                       ? local_only_message
                       : "this can only be done on the computer the server "
                         "runs on";
        return 403;
    }
    return 0;
}
