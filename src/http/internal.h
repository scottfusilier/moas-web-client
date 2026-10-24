/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Shared between the files of the HTTP server; not for use outside it. */

#ifndef MOAS_HTTP_INTERNAL_H
#define MOAS_HTTP_INTERNAL_H

#include "core/caps.h"
#include "http/http.h"

struct http_route {
    const char *method;
    const char *path;
    http_handler_fn *fn;
    void *arg;
    size_t max_body;
    bool local_only;
    const char *local_only_message;
};

struct http_router {
    struct http_route routes[CAPS_HTTP_ROUTES];
    size_t count;
};

/*
 * The route for method and path.  NULL if there is none, with *status 404
 * (no such path) or 405 (the path takes other methods, listed in allow).
 */
const struct http_route *http_router_match(const struct http_router *router,
                                           struct str method, struct str path,
                                           int *status, char *allow,
                                           size_t allow_size);

#endif /* MOAS_HTTP_INTERNAL_H */
