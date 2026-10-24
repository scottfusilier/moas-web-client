/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "http/http.h"
#include "http/internal.h"

#define DEFAULT_MAX_BODY 4096

int
http_router_create(struct http_router **out)
{
    struct http_router *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return ERR_NOMEM;
    }
    *out = r;
    return 0;
}

void
http_router_destroy(struct http_router *r)
{
    free(r);
}

int
http_router_add(struct http_router *r, const char *method, const char *path,
                const struct http_route_options *options, http_handler_fn *fn,
                void *arg)
{
    if (r->count >= CAPS_HTTP_ROUTES) {
        return ERR_LIMIT;
    }
    if (fn == NULL || method == NULL || path == NULL || path[0] != '/') {
        return ERR_INVALID;
    }
    struct http_route *route = &r->routes[r->count];
    memset(route, 0, sizeof(*route));
    route->method = method;
    route->path = path;
    route->fn = fn;
    route->arg = arg;
    route->max_body = DEFAULT_MAX_BODY;
    if (options != NULL) {
        if (options->max_body > CAPS_HTTP_BODY) {
            return ERR_LIMIT;
        }
        if (options->max_body != 0) {
            route->max_body = options->max_body;
        }
        route->local_only = options->local_only;
        route->local_only_message = options->local_only_message;
    }
    r->count++;
    return 0;
}

const struct http_route *
http_router_match(const struct http_router *r, struct str method,
                  struct str path, int *status, char *allow, size_t allow_size)
{
    bool path_known = false;
    allow[0] = '\0';
    /* HEAD is answered by the GET route. */
    struct str wanted = str_eq(method, STR_LIT("HEAD")) ? STR_LIT("GET")
                                                        : method;

    for (size_t i = 0; i < r->count; i++) {
        const struct http_route *route = &r->routes[i];
        if (!str_eq(path, str_from_cstr(route->path))) {
            continue;
        }
        if (str_eq(wanted, str_from_cstr(route->method))) {
            *status = 0;
            return route;
        }
        /* Collect the methods this path does take, for "Allow". */
        size_t used = strlen(allow);
        size_t add = strlen(route->method);
        if (used + add + 3 < allow_size) {
            if (used > 0) {
                memcpy(allow + used, ", ", 2);
                used += 2;
            }
            memcpy(allow + used, route->method, add + 1);
        }
        path_known = true;
    }
    *status = path_known ? 405 : 404;
    return NULL;
}
