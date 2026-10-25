/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/settings.h"
#include "core/json.h"

#define DEFAULT_SERVER "127.0.0.1:12059"
#define DEFAULT_LISTEN "127.0.0.1:8080"

/* Upper bounds beyond "1 or more", to keep numbers sane. */
#define MAX_STATION 255
#define MAX_RADIO 255
#define MAX_GRID 64

static int fail(struct app_field_error *err, const char *field,
                const char *fmt, ...) MOAS_PRINTF(3, 4);

static int
fail(struct app_field_error *err, const char *field, const char *fmt, ...)
{
    va_list ap;

    snprintf(err->field, sizeof(err->field), "%s", field);
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
    return ERR_INVALID;
}

void
app_settings_defaults(struct app_settings *s)
{
    memset(s, 0, sizeof(*s));
    s->version = APP_SETTINGS_FORMAT;
    snprintf(s->server, sizeof(s->server), "%s", DEFAULT_SERVER);
    plat_addr_parse(s->server, strlen(s->server), &s->server_addr);
    snprintf(s->listen, sizeof(s->listen), "%s", DEFAULT_LISTEN);
    plat_addr_parse(s->listen, strlen(s->listen), &s->listen_addr);
    s->station = 1;
    s->rows = 6;
    s->columns = 5;
    s->radios[0].radio = 1;
    s->radios[1].radio = 2;
    s->radio_count = 2;
}

static void
fini_radios(struct app_settings *s)
{
    for (size_t i = 0; i < s->radio_count; i++) {
        struct app_radio_settings *r = &s->radios[i];
        for (size_t j = 0; j < r->source_count; j++) {
            freq_source_config_fini(&r->sources[j]);
        }
        r->source_count = 0;
    }
    s->radio_count = 0;
}

void
app_settings_fini(struct app_settings *s)
{
    fini_radios(s);
}

/* Removes every "_comment" member, at any depth. */
static void
strip_comments(cJSON *item, bool *had)
{
    if (cJSON_IsObject(item)) {
        cJSON *child = item->child;
        while (child != NULL) {
            cJSON *next = child->next;
            if (child->string != NULL &&
                strcmp(child->string, "_comment") == 0)
            {
                *had = true;
                cJSON_Delete(cJSON_DetachItemViaPointer(item, child));
            } else {
                strip_comments(child, had);
            }
            child = next;
        }
    } else if (cJSON_IsArray(item)) {
        for (cJSON *child = item->child; child != NULL; child = child->next) {
            strip_comments(child, had);
        }
    }
}

/* Refuses members not in allowed[] (NULL-terminated). */
static int
check_keys(const cJSON *obj, const char *where, const char *const *allowed,
           struct app_field_error *err)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, obj)
    {
        const char *key = item->string != NULL ? item->string : "";
        bool known = false;
        for (size_t i = 0; allowed[i] != NULL && !known; i++) {
            known = strcmp(key, allowed[i]) == 0;
        }
        if (!known) {
            char shown[64];
            size_t n = strlen(key);
            str_copy_clean(str_make(key, n < 60 ? n : 60), shown,
                           sizeof(shown));
            return fail(err, where, "\"%s\" is not a setting%s%s", shown,
                        where[0] != '\0' ? " in " : "", where);
        }
    }
    return 0;
}

/* host:port with a numeric host, into text and addr. */
static int
read_address(const cJSON *json, const char *field, char *text, size_t size,
             struct plat_addr *addr, struct app_field_error *err)
{
    char given[PLAT_ADDR_TEXT_MAX];
    snprintf(given, sizeof(given), "%s", text);
    int e = json_get_text(json, field, given, sizeof(given));
    struct str a = str_trim(str_from_cstr(given));
    if (e != 0 || plat_addr_parse(a.ptr, a.len, addr) != 0) {
        return fail(err, field,
                    "%s \"%.*s\" is not host:port with a numeric host, for "
                    "example 127.0.0.1:12059",
                    field, (int)a.len, a.ptr);
    }
    snprintf(text, size, "%.*s", (int)a.len, a.ptr);
    return 0;
}

/* A whole number of at least 1 (and at most max). */
static int
read_count(const cJSON *json, const char *field, int max, int *out,
           struct app_field_error *err)
{
    int e = json_get_int(json, field, -1000000, 1000000, out);
    if (e != 0) {
        return fail(err, field, "%s must be a whole number", field);
    }
    if (*out < 1) {
        return fail(err, field, "%s must be 1 or more", field);
    }
    if (*out > max) {
        return fail(err, field, "%s can be at most %d", field, max);
    }
    return 0;
}

static int
read_radio(const cJSON *json, size_t index, struct app_radio_settings *r,
           const struct app_settings *s, struct app_field_error *err)
{
    static const char *const allowed[] = {"Radio", "Sources", "_comment",
                                          NULL};
    char path[48];

    snprintf(path, sizeof(path), "Radios.%u", (unsigned)index);
    if (!cJSON_IsObject(json)) {
        return fail(err, path, "each radio must be an object");
    }
    int e = check_keys(json, path, allowed, err);
    if (e != 0) {
        return e;
    }
    char field[64];
    snprintf(field, sizeof(field), "%s.Radio", path);
    r->radio = 0;
    if (json_get_int(json, "Radio", -1000000, 1000000, &r->radio) != 0) {
        return fail(err, field, "a radio number must be a whole number");
    }
    if (r->radio < 1 || r->radio > MAX_RADIO) {
        return fail(err, field,
                    "a radio number must be 1 or more (and at "
                    "most %d)",
                    MAX_RADIO);
    }
    for (size_t i = 0; i < index; i++) {
        if (s->radios[i].radio == r->radio) {
            return fail(err, field, "radio %d is listed twice", r->radio);
        }
    }

    const cJSON *sources;
    size_t count;
    e = json_get_array(json, "Sources", CAPS_FREQ_SOURCES, &sources, &count);
    if (e != 0) {
        snprintf(field, sizeof(field), "%s.Sources", path);
        return fail(err, field,
                    e == ERR_LIMIT ? "a radio can have at most %d sources"
                                   : "Sources must be a list (at most %d)",
                    CAPS_FREQ_SOURCES);
    }
    const cJSON *item;
    size_t j = 0;
    cJSON_ArrayForEach(item, sources)
    {
        struct freq_field_error fe;
        e = freq_parse_source(item, r->radio, &r->sources[j], &fe);
        if (e != 0) {
            snprintf(field, sizeof(field), "%s.Sources.%u%s%s", path,
                     (unsigned)j, fe.field[0] != '\0' ? "." : "", fe.field);
            return fail(err, field, "radio %d, source %u: %s", r->radio,
                        (unsigned)j + 1, fe.message);
        }
        r->source_count = ++j;
    }
    return 0;
}

static int
apply_json(const cJSON *json, struct app_settings *s, bool *has_radios,
           struct app_field_error *err)
{
    static const char *const allowed[] = {
        "Version", "Server", "Listen",        "Station",  "Rows",
        "Columns", "Radios", "RecentServers", "_comment", NULL};

    if (!cJSON_IsObject(json)) {
        return fail(err, "", "the settings must be a JSON object");
    }
    int e = check_keys(json, "", allowed, err);
    if (e != 0) {
        return e;
    }

    /* A file without a Version is from before it existed: Version 1. */
    int version = 1;
    if (json_get_int(json, "Version", -1000000, 1000000, &version) != 0) {
        return fail(err, "Version", "Version must be a whole number");
    }
    if (version > APP_SETTINGS_FORMAT) {
        return fail(
            err, "Version",
            "the file is Version %d, written by a newer moas-web-client; "
            "this one understands up to %d",
            version, APP_SETTINGS_FORMAT);
    }
    if (version < 1) {
        return fail(err, "Version",
                    "Version %d is not a settings file version", version);
    }
    s->version = APP_SETTINGS_FORMAT;

    if ((e = read_address(json, "Server", s->server, sizeof(s->server),
                          &s->server_addr, err)) != 0 ||
        (e = read_address(json, "Listen", s->listen, sizeof(s->listen),
                          &s->listen_addr, err)) != 0 ||
        (e = read_count(json, "Station", MAX_STATION, &s->station, err)) !=
            0 ||
        (e = read_count(json, "Rows", MAX_GRID, &s->rows, err)) != 0 ||
        (e = read_count(json, "Columns", MAX_GRID, &s->columns, err)) != 0)
    {
        return e;
    }

    const cJSON *radios;
    size_t count;
    *has_radios = cJSON_GetObjectItemCaseSensitive(json, "Radios") != NULL &&
                  !cJSON_IsNull(
                      cJSON_GetObjectItemCaseSensitive(json, "Radios"));
    e = json_get_array(json, "Radios", CAPS_OASP_RADIOS, &radios, &count);
    if (e != 0) {
        return fail(err, "Radios",
                    e == ERR_LIMIT ? "there can be at most %d radios"
                                   : "Radios must be a list (at most %d)",
                    CAPS_OASP_RADIOS);
    }
    if (*has_radios) {
        if (count == 0) {
            return fail(err, "Radios", "Radios must list at least one radio");
        }
        fini_radios(s);
        const cJSON *item;
        size_t i = 0;
        cJSON_ArrayForEach(item, radios)
        {
            memset(&s->radios[i], 0, sizeof(s->radios[i]));
            s->radio_count = i + 1;
            e = read_radio(item, i, &s->radios[i], s, err);
            if (e != 0) {
                return e;
            }
            i++;
        }
    }

    const cJSON *recent;
    e = json_get_array(json, "RecentServers", 1000, &recent, &count);
    if (e != 0) {
        return fail(err, "RecentServers", "RecentServers must be a list");
    }
    if (cJSON_GetObjectItemCaseSensitive(json, "RecentServers") != NULL) {
        s->recent_count = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, recent)
        {
            if (s->recent_count >= APP_RECENT_SERVERS) {
                break;
            }
            if (!cJSON_IsString(item) || item->valuestring == NULL ||
                str_copy_clean(str_from_cstr(item->valuestring),
                               s->recent[s->recent_count],
                               sizeof(s->recent[0])) != 0)
            {
                continue; /* not worth refusing the file over */
            }
            s->recent_count++;
        }
    }
    return 0;
}

int
app_settings_from_json(const cJSON *json, struct app_settings *s,
                       bool *has_radios, struct app_field_error *err)
{
    struct app_settings work;
    bool radios = false;

    memset(err, 0, sizeof(*err));
    int e = app_settings_copy(&work, s);
    if (e != 0) {
        return fail(err, "", "out of memory");
    }
    e = apply_json(json, &work, &radios, err);
    if (e != 0) {
        app_settings_fini(&work);
        return e;
    }
    app_settings_fini(s);
    *s = work;
    if (has_radios != NULL) {
        *has_radios = radios;
    }
    return 0;
}

int
app_settings_parse(struct str text, struct app_settings *s, bool *has_radios,
                   bool *had_comments, struct app_field_error *err)
{
    cJSON *json = NULL;
    bool had = false;

    memset(err, 0, sizeof(*err));
    if (json_parse(text, &json) != 0) {
        return fail(err, "", "the file is not valid JSON");
    }
    strip_comments(json, &had);
    if (had_comments != NULL) {
        *had_comments = had;
    }
    int e = app_settings_from_json(json, s, has_radios, err);
    cJSON_Delete(json);
    return e;
}

void
app_settings_parse_partial(struct str text, struct app_settings *s)
{
    cJSON *json = NULL;
    bool had = false;
    bool radios = false;
    struct app_field_error err;
    struct app_settings work;

    if (json_parse(text, &json) != 0) {
        return;
    }
    strip_comments(json, &had);
    if (app_settings_copy(&work, s) == 0) {
        /* Whatever was applied before the problem stays. */
        apply_json(json, &work, &radios, &err);
        app_settings_fini(s);
        *s = work;
    }
    cJSON_Delete(json);
}

cJSON *
app_settings_to_json(const struct app_settings *s)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    bool ok = cJSON_AddNumberToObject(root, "Version", APP_SETTINGS_FORMAT) !=
                  NULL &&
              cJSON_AddStringToObject(root, "Server", s->server) != NULL &&
              cJSON_AddStringToObject(root, "Listen", s->listen) != NULL &&
              cJSON_AddNumberToObject(root, "Station", s->station) != NULL &&
              cJSON_AddNumberToObject(root, "Rows", s->rows) != NULL &&
              cJSON_AddNumberToObject(root, "Columns", s->columns) != NULL;
    cJSON *radios = ok ? cJSON_AddArrayToObject(root, "Radios") : NULL;
    ok = radios != NULL;
    for (size_t i = 0; ok && i < s->radio_count; i++) {
        const struct app_radio_settings *r = &s->radios[i];
        cJSON *radio = cJSON_CreateObject();
        ok = radio != NULL && cJSON_AddItemToArray(radios, radio) &&
             cJSON_AddNumberToObject(radio, "Radio", r->radio) != NULL;
        cJSON *sources = ok ? cJSON_AddArrayToObject(radio, "Sources") : NULL;
        ok = sources != NULL;
        for (size_t j = 0; ok && j < r->source_count; j++) {
            cJSON *src = freq_source_to_json(&r->sources[j]);
            ok = src != NULL && cJSON_AddItemToArray(sources, src);
        }
    }
    if (ok && s->recent_count > 0) {
        cJSON *recent = cJSON_AddArrayToObject(root, "RecentServers");
        ok = recent != NULL;
        for (size_t i = 0; ok && i < s->recent_count; i++) {
            ok = cJSON_AddItemToArray(recent,
                                      cJSON_CreateString(s->recent[i]));
        }
    }
    if (!ok) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

int
app_settings_copy(struct app_settings *dst, const struct app_settings *src)
{
    *dst = *src;
    /* The sources own memory: copy them by writing and reading them again. */
    for (size_t i = 0; i < src->radio_count; i++) {
        dst->radios[i].source_count = 0;
    }
    for (size_t i = 0; i < src->radio_count; i++) {
        const struct app_radio_settings *from = &src->radios[i];
        struct app_radio_settings *to = &dst->radios[i];
        for (size_t j = 0; j < from->source_count; j++) {
            cJSON *json = freq_source_to_json(&from->sources[j]);
            struct freq_field_error fe;
            int e = json == NULL ? ERR_NOMEM
                                 : freq_parse_source(json, from->radio,
                                                     &to->sources[j], &fe);
            cJSON_Delete(json);
            if (e != 0) {
                app_settings_fini(dst);
                return e;
            }
            to->source_count = j + 1;
        }
    }
    return 0;
}

/* The source's settings as compact JSON text, for comparing. */
static char *
source_text(const struct freq_source_config *c)
{
    cJSON *json = freq_source_to_json(c);
    char *text = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    return text;
}

bool
app_settings_same_sources(const struct app_settings *a,
                          const struct app_settings *b)
{
    if (a->station != b->station || a->radio_count != b->radio_count) {
        return false;
    }
    for (size_t i = 0; i < a->radio_count; i++) {
        const struct app_radio_settings *x = &a->radios[i];
        const struct app_radio_settings *y = &b->radios[i];
        if (x->radio != y->radio || x->source_count != y->source_count) {
            return false;
        }
        for (size_t j = 0; j < x->source_count; j++) {
            char *p = source_text(&x->sources[j]);
            char *q = source_text(&y->sources[j]);
            bool same = p != NULL && q != NULL && strcmp(p, q) == 0;
            cJSON_free(p);
            cJSON_free(q);
            if (!same) {
                return false;
            }
        }
    }
    return true;
}

void
app_settings_remember_server(struct app_settings *s, const char *server)
{
    char recent[APP_RECENT_SERVERS][PLAT_ADDR_TEXT_MAX];
    size_t n = 0;

    snprintf(recent[n++], sizeof(recent[0]), "%s", server);
    for (size_t i = 0; i < s->recent_count && n < APP_RECENT_SERVERS; i++) {
        if (strcmp(s->recent[i], server) != 0) {
            memcpy(recent[n++], s->recent[i], sizeof(recent[0]));
        }
    }
    memcpy(s->recent, recent, sizeof(recent));
    s->recent_count = n;
}
