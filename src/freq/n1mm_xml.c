/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <string.h>

#include "freq/n1mm_xml.h"

/* Longest element value kept (the station name is the longest used). */
#define VALUE_MAX CAPS_N1MM_STATION

/* A reading position in the packet. */
struct cursor {
    const char *p;
    const char *end;
};

static bool
at_end(const struct cursor *c)
{
    return c->p >= c->end;
}

static bool
is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void
skip_space(struct cursor *c)
{
    while (!at_end(c) && is_space(*c->p)) {
        c->p++;
    }
}

/* Consumes text if it comes next. */
static bool
take(struct cursor *c, const char *text)
{
    size_t n = strlen(text);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, text, n) != 0) {
        return false;
    }
    c->p += n;
    return true;
}

static bool
is_name_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
}

/* An element name; false if there is none. */
static bool
take_name(struct cursor *c, struct str *name)
{
    const char *start = c->p;
    while (!at_end(c) && is_name_char(*c->p)) {
        c->p++;
    }
    *name = str_make(start, (size_t)(c->p - start));
    return name->len > 0;
}

/*
 * After an element's name: attributes, if any, up to '>' or "/>".  Sets
 * *empty for "/>".  Attributes are skipped, not read; a quoted value may
 * hold '>' or '/'.
 */
static bool
take_tag_end(struct cursor *c, bool *empty)
{
    char quote = '\0';
    while (!at_end(c)) {
        char ch = *c->p++;
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '<') {
            return false;
        } else if (ch == '>') {
            *empty = false;
            return true;
        } else if (ch == '/' && !at_end(c) && *c->p == '>') {
            c->p++;
            *empty = true;
            return true;
        }
    }
    return false;
}

/*
 * Decodes text up to the next '<' into out (size bytes), replacing XML's
 * five named entities.  Numeric references (&#...;) become '?'.  Text too
 * long for out is cut short; it is only kept for the names used here.
 */
static bool
take_text(struct cursor *c, char *out, size_t size)
{
    static const struct {
        const char *name;
        char ch;
    } entities[] = {
        {"&amp;", '&'},  {"&lt;", '<'},    {"&gt;", '>'},
        {"&quot;", '"'}, {"&apos;", '\''},
    };
    size_t n = 0;

    while (!at_end(c) && *c->p != '<') {
        char ch = *c->p;
        if (ch == '&') {
            bool known = false;
            for (size_t i = 0; i < ARRAY_LEN(entities) && !known; i++) {
                if (take(c, entities[i].name)) {
                    ch = entities[i].ch;
                    known = true;
                }
            }
            if (!known) {
                if (!take(c, "&#")) {
                    return false;
                }
                /* &#123; or &#x1F; */
                while (!at_end(c) && *c->p != ';' && *c->p != '<') {
                    c->p++;
                }
                if (!take(c, ";")) {
                    return false;
                }
                ch = '?';
            }
        } else {
            c->p++;
        }
        if (n + 1 < size) {
            out[n++] = ch;
        }
    }
    out[n] = '\0';
    return true;
}

static bool
name_is(struct str name, const char *want)
{
    return str_eq(name, str_from_cstr(want));
}

/* A number made only of digits, within min..max; empty counts as 0. */
static int
parse_number(const char *text, int64_t max, int64_t *out)
{
    struct str s = str_trim(str_from_cstr(text));
    if (s.len == 0) {
        *out = 0;
        return 0;
    }
    return str_to_i64(s, 0, max, out) == 0 ? 0 : ERR_INVALID;
}

int
n1mm_parse_radio_info(struct str packet, struct n1mm_radio_info *out)
{
    struct cursor c = {packet.ptr, packet.ptr + packet.len};
    struct str name;
    bool empty = false;

    memset(out, 0, sizeof(*out));
    out->connected = true;

    /* A UTF-8 byte order mark, and the XML declaration, may come first. */
    take(&c, "\xef\xbb\xbf");
    skip_space(&c);
    if (take(&c, "<?")) {
        while (!at_end(&c) && !take(&c, "?>")) {
            c.p++;
        }
        skip_space(&c);
    }

    if (!take(&c, "<") || !take_name(&c, &name) ||
        !name_is(name, "RadioInfo") || !take_tag_end(&c, &empty))
    {
        return ERR_INVALID;
    }

    char value[VALUE_MAX];
    while (!empty) {
        skip_space(&c);
        if (take(&c, "</")) {
            if (!take_name(&c, &name) || !name_is(name, "RadioInfo")) {
                return ERR_INVALID;
            }
            skip_space(&c);
            if (!take(&c, ">")) {
                return ERR_INVALID;
            }
            break;
        }
        /* One child element: <Name>text</Name> or <Name/>. */
        bool child_empty = false;
        if (!take(&c, "<") || !take_name(&c, &name) ||
            !take_tag_end(&c, &child_empty))
        {
            return ERR_INVALID;
        }
        value[0] = '\0';
        if (!child_empty) {
            struct str closing;
            if (!take_text(&c, value, sizeof(value)) || !take(&c, "</") ||
                !take_name(&c, &closing) || !str_eq(closing, name))
            {
                return ERR_INVALID;
            }
            skip_space(&c);
            if (!take(&c, ">")) {
                return ERR_INVALID;
            }
        }

        int64_t number = 0;
        if (name_is(name, "StationName")) {
            struct str s = str_trim(str_from_cstr(value));
            if (str_copy_clean(s, out->station_name,
                               sizeof(out->station_name)) != 0)
            {
                return ERR_INVALID;
            }
        } else if (name_is(name, "RadioNr")) {
            if (parse_number(value, 1000, &number) != 0) {
                return ERR_INVALID;
            }
            out->radio_nr = (int)number;
        } else if (name_is(name, "Freq")) {
            /*
             * Up to 1 THz (10^11 units of 10 Hz): far beyond any radio, and
             * small enough that the value in kHz fits an int.
             */
            if (parse_number(value, 100000000000LL, &number) != 0) {
                return ERR_INVALID;
            }
            out->freq = number;
        } else if (name_is(name, "IsConnected")) {
            out->connected = !str_eq_nocase(str_trim(str_from_cstr(value)),
                                            STR_LIT("false"));
        }
    }

    /* Only whitespace (or NUL padding) may follow. */
    while (!at_end(&c)) {
        if (!is_space(*c.p) && *c.p != '\0') {
            return ERR_INVALID;
        }
        c.p++;
    }
    return 0;
}

int
n1mm_khz(const struct n1mm_radio_info *info)
{
    return (int)(info->freq / 100);
}
