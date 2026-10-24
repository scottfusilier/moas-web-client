/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "http/parse.h"
#include "picohttpparser.h"

/* Fails with an HTTP status. */
#define REFUSE(code)                                                          \
    do {                                                                      \
        *status = (code);                                                     \
        return ERR_INVALID;                                                   \
    } while (0)

static bool
is_token_upper(struct str s)
{
    if (s.len == 0 || s.len > 16) {
        return false;
    }
    for (size_t i = 0; i < s.len; i++) {
        if (s.ptr[i] < 'A' || s.ptr[i] > 'Z') {
            return false;
        }
    }
    return true;
}

/* Visible ASCII only: no spaces, controls or bytes above 0x7e. */
static bool
is_plain_target(struct str s)
{
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (c < 0x21 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

/* No control characters but tab (and no DEL). */
static bool
is_clean_value(struct str s)
{
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return false;
        }
    }
    return true;
}

/* True if the comma-separated list holds token (any case). */
static bool
list_has(struct str list, struct str token)
{
    struct str rest = list;
    for (;;) {
        struct str item;
        struct str after;
        bool more = str_split(rest, ',', &item, &after);
        if (!more) {
            item = rest;
        }
        if (str_eq_nocase(str_trim(item), token)) {
            return true;
        }
        if (!more) {
            return false;
        }
        rest = after;
    }
}

struct str
http_header_value(const struct http_request *req, const char *name)
{
    struct str n = str_from_cstr(name);
    for (size_t i = 0; i < req->header_count; i++) {
        if (str_eq_nocase(req->headers[i].name, n)) {
            return req->headers[i].value;
        }
    }
    return str_make(NULL, 0);
}

static size_t
count_headers(const struct http_request *req, const char *name)
{
    struct str n = str_from_cstr(name);
    size_t count = 0;
    for (size_t i = 0; i < req->header_count; i++) {
        if (str_eq_nocase(req->headers[i].name, n)) {
            count++;
        }
    }
    return count;
}

int
http_parse_head(struct str data, struct http_request *req, size_t *head_len,
                int *status)
{
    memset(req, 0, sizeof(*req));
    *status = 0;

    const char *method;
    size_t method_len;
    const char *target;
    size_t target_len;
    int minor = -1;
    struct phr_header headers[CAPS_HTTP_HEADERS];
    size_t num_headers = CAPS_HTTP_HEADERS;

    /* Never look beyond the limit, so a huge head costs nothing to refuse. */
    size_t len = data.len < CAPS_HTTP_HEAD ? data.len : CAPS_HTTP_HEAD;
    int n = phr_parse_request(data.ptr, len, &method, &method_len, &target,
                              &target_len, &minor, headers, &num_headers, 0);
    if (n == -2) {
        if (data.len >= CAPS_HTTP_HEAD) {
            REFUSE(431);
        }
        return ERR_AGAIN;
    }
    if (n < 0) {
        /* Includes more than CAPS_HTTP_HEADERS headers. */
        REFUSE(400);
    }

    req->method = str_make(method, method_len);
    req->target = str_make(target, target_len);
    req->minor_version = minor;
    if (minor != 0 && minor != 1) {
        REFUSE(505);
    }
    if (!is_token_upper(req->method)) {
        REFUSE(400);
    }
    /* Only a plain path: no "http://host/..." forms, no fragment. */
    if (target_len == 0 || target_len > CAPS_HTTP_TARGET || target[0] != '/' ||
        !is_plain_target(req->target) ||
        memchr(target, '#', target_len) != NULL)
    {
        REFUSE(400);
    }
    if (!str_split(req->target, '?', &req->path, &req->query)) {
        req->path = req->target;
        req->query = str_make(target + target_len, 0);
    }

    for (size_t i = 0; i < num_headers; i++) {
        /* A NULL name is a folded continuation line: not accepted. */
        if (headers[i].name == NULL || headers[i].name_len == 0) {
            REFUSE(400);
        }
        struct str value = str_make(headers[i].value, headers[i].value_len);
        if (!is_clean_value(value)) {
            REFUSE(400);
        }
        req->headers[i].name = str_make(headers[i].name, headers[i].name_len);
        req->headers[i].value = value;
    }
    req->header_count = num_headers;

    /* Requests whose length is not plainly stated are refused outright. */
    if (http_header_value(req, "Transfer-Encoding").ptr != NULL) {
        REFUSE(501);
    }
    size_t lengths = count_headers(req, "Content-Length");
    if (lengths > 1) {
        REFUSE(400);
    }
    if (lengths == 1) {
        int64_t cl = 0;
        int err = str_to_i64(
            str_trim(http_header_value(req, "Content-Length")), 0, INT64_MAX,
            &cl);
        if (err != 0) {
            REFUSE(400);
        }
        if (cl > CAPS_HTTP_BODY) {
            REFUSE(413);
        }
        req->content_length = (size_t)cl;
    }

    size_t hosts = count_headers(req, "Host");
    if (hosts > 1 || (minor == 1 && hosts == 0)) {
        REFUSE(400);
    }

    struct str connection = http_header_value(req, "Connection");
    if (minor == 1) {
        req->keep_alive = !list_has(connection, STR_LIT("close"));
    } else {
        req->keep_alive = list_has(connection, STR_LIT("keep-alive"));
    }

    *head_len = (size_t)n;
    return 0;
}
