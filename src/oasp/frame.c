/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include "core/caps.h"
#include "oasp/frame.h"

void
oasp_framer_reset(struct oasp_framer *f)
{
    f->pos = 0;
    f->start = 0;
    f->depth = 0;
    f->in_string = false;
    f->escape = false;
}

static bool
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int
oasp_frame_next(struct oasp_framer *f, const char *data, size_t len,
                size_t *start, size_t *end)
{
    while (f->pos < len) {
        char c = data[f->pos];

        if (f->depth == 0) {
            /* Between objects: only whitespace, then '{'. */
            if (is_space(c)) {
                f->pos++;
                continue;
            }
            if (c != '{') {
                return ERR_INVALID;
            }
            f->start = f->pos;
            f->depth = 1;
            f->pos++;
            continue;
        }

        if (f->pos - f->start >= CAPS_OASP_MESSAGE) {
            return ERR_LIMIT;
        }
        f->pos++;

        if (f->in_string) {
            if (f->escape) {
                f->escape = false;
            } else if (c == '\\') {
                f->escape = true;
            } else if (c == '"') {
                f->in_string = false;
            }
            continue;
        }

        switch (c) {
        case '"':
            f->in_string = true;
            break;
        case '{':
        case '[':
            if (f->depth >= CAPS_JSON_DEPTH) {
                return ERR_LIMIT;
            }
            f->depth++;
            break;
        case '}':
        case ']':
            f->depth--;
            if (f->depth == 0) {
                if (c != '}') {
                    return ERR_INVALID;
                }
                *start = f->start;
                *end = f->pos;
                return 0;
            }
            break;
        default:
            break;
        }
    }
    return ERR_AGAIN;
}
