/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Reading the head of an HTTP/1.x request: the request line and headers.
 * picohttpparser splits them up; this file then applies the server's own,
 * stricter rules, so that only plain requests of the kind a browser sends
 * get through.  No I/O happens here, so it can be fuzzed.
 *
 * Everything in a struct http_request is a slice of the bytes given to
 * http_parse_head(), valid as long as they are.
 */

#ifndef MOAS_HTTP_PARSE_H
#define MOAS_HTTP_PARSE_H

#include "core/caps.h"
#include "core/common.h"
#include "core/str.h"
#include "platform/net.h"

struct http_header {
    struct str name;
    struct str value;
};

struct http_request {
    struct str method; /* "GET", "POST", ... */
    struct str target; /* "/api/state?x=1" */
    struct str path;   /* "/api/state" */
    struct str query;  /* "x=1"; empty if none */
    int minor_version; /* 0 or 1, for HTTP/1.0 or 1.1 */
    struct http_header headers[CAPS_HTTP_HEADERS];
    size_t header_count;
    size_t content_length; /* 0 if there is no body */
    bool keep_alive;       /* what the client asked for */
    struct str body;       /* set by the server once the body has arrived */

    /* Set by the server: who sent the request. */
    struct plat_addr peer;
    bool peer_is_loopback;
};

/*
 * Reads the head at the start of data.  Returns 0 with *head_len the bytes
 * it took; ERR_AGAIN if it has not all arrived yet; or another error with
 * *status the HTTP status to answer with:
 *
 *   400  malformed, or not something this server accepts (a target that
 *        is not a plain path, control characters, a missing or repeated
 *        Host, a repeated Content-Length, ...)
 *   413  a Content-Length above CAPS_HTTP_BODY
 *   431  a head larger than CAPS_HTTP_HEAD
 *   501  Transfer-Encoding (chunked bodies are not accepted)
 *   505  not HTTP/1.0 or 1.1
 */
int http_parse_head(struct str data, struct http_request *req,
                    size_t *head_len, int *status);

/* The first header called name (any case); ptr is NULL if there is none. */
struct str http_header_value(const struct http_request *req, const char *name);

#endif /* MOAS_HTTP_PARSE_H */
