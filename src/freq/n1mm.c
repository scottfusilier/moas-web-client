/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/str.h"
#include "freq/n1mm.h"
#include "freq/n1mm_xml.h"
#include "platform/clock.h"

/* Wait before trying to open an address again. */
#define RETRY_MS 2000

/* Packets read per wake-up, so that a flood cannot starve the loop. */
#define PACKETS_PER_WAKE 32

struct n1mm_config {
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    int radio_nr;
    char station[CAPS_N1MM_STATION]; /* "" accepts any */
};

struct n1mm_listener;

struct n1mm_source {
    struct freq_source base;
    struct n1mm_listener *listener;
    struct n1mm_source *next; /* the listener's other sources */
    int radio_nr;
    char station[CAPS_N1MM_STATION];
    struct freq_feed feed;
};

/* One UDP address, shared by every source that uses it. */
struct n1mm_listener {
    struct n1mm_listener *next; /* the env's other listeners */
    struct loop *loop;
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    plat_socket sock;
    struct loop_timer retry;
    char error[CAPS_FREQ_ERROR]; /* why it is not listening; "" if it is */
    struct n1mm_source *sources;
    struct freq_n1mm_heard heard[CAPS_N1MM_HEARD];
    size_t heard_count;
    char packet[CAPS_N1MM_PACKET];
};

/* The env's shared state for this type: its listeners. */
struct n1mm_shared {
    struct n1mm_listener *listeners;
};

static bool
same_addr(const struct plat_addr *a, const struct plat_addr *b)
{
    return a->len == b->len && memcmp(a->u.bytes, b->u.bytes, a->len) == 0;
}

static void
fail_all(struct n1mm_listener *l)
{
    for (struct n1mm_source *s = l->sources; s != NULL; s = s->next) {
        freq_feed_fail(s->feed, l->error);
    }
}

static void
remember(struct n1mm_listener *l, const struct n1mm_radio_info *info)
{
    struct freq_n1mm_heard *slot = NULL;
    for (size_t i = 0; i < l->heard_count && slot == NULL; i++) {
        struct freq_n1mm_heard *h = &l->heard[i];
        if (h->radio_nr == info->radio_nr &&
            str_eq_nocase(str_from_cstr(h->station_name),
                          str_from_cstr(info->station_name)))
        {
            slot = h;
        }
    }
    if (slot == NULL && l->heard_count < CAPS_N1MM_HEARD) {
        slot = &l->heard[l->heard_count++];
    }
    if (slot == NULL) {
        /* Full: replace the one heard longest ago. */
        slot = &l->heard[0];
        for (size_t i = 1; i < l->heard_count; i++) {
            if (l->heard[i].at_ms < slot->at_ms) {
                slot = &l->heard[i];
            }
        }
    }
    memcpy(slot->station_name, info->station_name, sizeof(slot->station_name));
    slot->radio_nr = info->radio_nr;
    slot->khz = n1mm_khz(info);
    slot->connected = info->connected;
    slot->at_ms = plat_monotonic_ms();
}

static void
deliver(struct n1mm_listener *l, const struct n1mm_radio_info *info)
{
    remember(l, info);
    struct str station = str_from_cstr(info->station_name);
    for (struct n1mm_source *s = l->sources; s != NULL; s = s->next) {
        if (s->radio_nr != info->radio_nr) {
            continue;
        }
        if (s->station[0] != '\0' &&
            !str_eq_nocase(str_from_cstr(s->station), station))
        {
            continue;
        }
        if (!info->connected) {
            freq_feed_fail(s->feed, "N1MM says the radio is not connected");
        } else if (n1mm_khz(info) > 0) {
            freq_feed_report(s->feed, n1mm_khz(info));
        }
    }
}

static void listen_now(struct n1mm_listener *l);

static void
give_up(struct n1mm_listener *l, int err)
{
    if (l->sock != PLAT_INVALID_SOCKET) {
        loop_unwatch(l->loop, l->sock);
        plat_socket_close(l->sock);
        l->sock = PLAT_INVALID_SOCKET;
    }
    snprintf(l->error, sizeof(l->error), "cannot listen for N1MM on %s: %s",
             l->addr_text, err_str(err));
    fail_all(l);
    loop_timer_start(l->loop, &l->retry, RETRY_MS, 0);
}

static void
on_packet(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)events;
    struct n1mm_listener *l = arg;

    for (int i = 0; i < PACKETS_PER_WAKE; i++) {
        size_t got = 0;
        int err = plat_recvfrom(sock, l->packet, sizeof(l->packet), &got,
                                NULL);
        if (err == ERR_AGAIN) {
            return;
        }
        if (err == ERR_LIMIT) {
            continue; /* too big to be RadioInfo */
        }
        if (err != 0) {
            give_up(l, err);
            return;
        }
        struct n1mm_radio_info info;
        if (n1mm_parse_radio_info(str_make(l->packet, got), &info) == 0) {
            deliver(l, &info);
        }
        /* Anything else is another N1MM packet type, or not N1MM at all. */
    }
}

static void
listen_now(struct n1mm_listener *l)
{
    int err = plat_udp_bind(&l->addr, &l->sock);
    if (err == 0) {
        err = loop_watch(l->loop, l->sock, LOOP_READ, on_packet, l);
    }
    if (err != 0) {
        give_up(l, err);
        return;
    }
    l->error[0] = '\0';
}

static void
on_retry(struct loop *loop, void *arg)
{
    (void)loop;
    listen_now(arg);
}

static struct n1mm_listener *
find_or_open(struct freq_env *env, const struct n1mm_config *cfg)
{
    void **slot = freq_env_shared(env, &freq_n1mm_ops);
    if (*slot == NULL) {
        *slot = calloc(1, sizeof(struct n1mm_shared));
        if (*slot == NULL) {
            return NULL;
        }
    }
    struct n1mm_shared *shared = *slot;
    for (struct n1mm_listener *l = shared->listeners; l != NULL; l = l->next) {
        if (same_addr(&l->addr, &cfg->addr)) {
            return l;
        }
    }
    struct n1mm_listener *l = calloc(1, sizeof(*l));
    if (l == NULL) {
        return NULL;
    }
    l->loop = freq_env_loop(env);
    l->addr = cfg->addr;
    memcpy(l->addr_text, cfg->addr_text, sizeof(l->addr_text));
    l->sock = PLAT_INVALID_SOCKET;
    loop_timer_init(&l->retry, on_retry, l);
    l->next = shared->listeners;
    shared->listeners = l;
    listen_now(l);
    return l;
}

static void
close_listener(struct freq_env *env, struct n1mm_listener *l)
{
    struct n1mm_shared *shared = *freq_env_shared(env, &freq_n1mm_ops);
    for (struct n1mm_listener **p = &shared->listeners; *p != NULL;
         p = &(*p)->next)
    {
        if (*p == l) {
            *p = l->next;
            break;
        }
    }
    if (l->sock != PLAT_INVALID_SOCKET) {
        loop_unwatch(l->loop, l->sock);
        plat_socket_close(l->sock);
    }
    loop_timer_stop(l->loop, &l->retry);
    free(l);
}

/* The env is needed again when the last source stops. */
struct n1mm_running {
    struct n1mm_source source;
    struct freq_env *env;
};

static int
n1mm_start(struct freq_env *env, const struct freq_source_config *config,
           struct freq_feed feed, struct freq_source **out)
{
    const struct n1mm_config *cfg = config->data;
    struct n1mm_running *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return ERR_NOMEM;
    }
    struct n1mm_listener *l = find_or_open(env, cfg);
    if (l == NULL) {
        free(r);
        return ERR_NOMEM;
    }
    r->env = env;
    struct n1mm_source *s = &r->source;
    s->base.ops = &freq_n1mm_ops;
    s->listener = l;
    s->radio_nr = cfg->radio_nr;
    memcpy(s->station, cfg->station, sizeof(s->station));
    s->feed = feed;
    s->next = l->sources;
    l->sources = s;
    if (l->error[0] != '\0') {
        freq_feed_fail(feed, l->error);
    }
    *out = &s->base;
    return 0;
}

static void
n1mm_stop(struct freq_source *base)
{
    struct n1mm_running *r = (struct n1mm_running *)(void *)base;
    struct n1mm_listener *l = r->source.listener;
    for (struct n1mm_source **p = &l->sources; *p != NULL; p = &(*p)->next) {
        if (*p == &r->source) {
            *p = r->source.next;
            break;
        }
    }
    if (l->sources == NULL) {
        close_listener(r->env, l);
    }
    free(r);
}

static void
n1mm_env_fini(void *shared)
{
    struct n1mm_shared *s = shared;
    /* Normally empty: every source stopped first. */
    while (s->listeners != NULL) {
        struct n1mm_listener *l = s->listeners;
        s->listeners = l->next;
        if (l->sock != PLAT_INVALID_SOCKET) {
            loop_unwatch(l->loop, l->sock);
            plat_socket_close(l->sock);
        }
        loop_timer_stop(l->loop, &l->retry);
        free(l);
    }
    free(s);
}

static int
n1mm_parse(const cJSON *json, int moas_radio,
           struct freq_source_config *config, struct freq_field_error *err)
{
    static const char *const allowed[] = {"Address", "RadioNr", "StationName",
                                          "TimeoutSeconds", NULL};
    /* Settings of another type get a clearer message than unknown ones. */
    if (cJSON_GetObjectItemCaseSensitive(json, "PollMilliseconds") != NULL) {
        return freq_field_error(err, "PollMilliseconds",
                                "PollMilliseconds does not apply to n1mm");
    }
    int e = freq_check_keys(json, allowed, err);
    if (e != 0) {
        return e;
    }

    struct n1mm_config *cfg = calloc(1, sizeof(*cfg));
    if (cfg == NULL) {
        return ERR_NOMEM;
    }
    config->data = cfg;

    e = freq_parse_address(json, FREQ_N1MM_DEFAULT_ADDRESS, &cfg->addr,
                           cfg->addr_text, sizeof(cfg->addr_text), err);
    if (e != 0) {
        return e;
    }
    cfg->radio_nr = 0;
    if (json_get_int(json, "RadioNr", -1000000, 1000000, &cfg->radio_nr) != 0)
    {
        return freq_field_error(err, "RadioNr", "RadioNr must be a number");
    }
    if (cfg->radio_nr == 0) {
        cfg->radio_nr = moas_radio;
    }
    if (cfg->radio_nr < 1 || cfg->radio_nr > 1000) {
        return freq_field_error(err, "RadioNr",
                                "RadioNr must be 1 or more (and at most "
                                "1000)");
    }
    char station[CAPS_N1MM_STATION] = "";
    if (json_get_text(json, "StationName", station, sizeof(station)) != 0) {
        return freq_field_error(err, "StationName",
                                "StationName must be text of at most %d "
                                "bytes",
                                CAPS_N1MM_STATION - 1);
    }
    struct str st = str_trim(str_from_cstr(station));
    memcpy(cfg->station, st.ptr, st.len);
    cfg->station[st.len] = '\0';

    e = freq_parse_timeout(json, 0, &config->timeout_ms, err);
    if (e != 0) {
        return e;
    }
    if (cfg->station[0] != '\0') {
        snprintf(config->name, sizeof(config->name), "N1MM %s radio %d",
                 cfg->station, cfg->radio_nr);
    } else {
        snprintf(config->name, sizeof(config->name), "N1MM radio %d",
                 cfg->radio_nr);
    }
    return 0;
}

static void
n1mm_free_data(void *data)
{
    free(data);
}

const struct freq_source_ops freq_n1mm_ops = {
    .type = "n1mm",
    .parse = n1mm_parse,
    .free_data = n1mm_free_data,
    .start = n1mm_start,
    .stop = n1mm_stop,
    .env_fini = n1mm_env_fini,
};

static int
compare_heard(const void *a, const void *b)
{
    const struct freq_n1mm_heard *x = a;
    const struct freq_n1mm_heard *y = b;
    int c = strcmp(x->station_name, y->station_name);
    if (c != 0) {
        return c;
    }
    return x->radio_nr < y->radio_nr ? -1 : x->radio_nr > y->radio_nr;
}

size_t
freq_n1mm_heard(struct freq_env *env, const struct plat_addr *addr,
                struct freq_n1mm_heard *out, size_t max)
{
    void **slot = freq_env_shared(env, &freq_n1mm_ops);
    if (*slot == NULL) {
        return 0;
    }
    const struct n1mm_shared *shared = *slot;
    for (const struct n1mm_listener *l = shared->listeners; l != NULL;
         l = l->next)
    {
        if (!same_addr(&l->addr, addr)) {
            continue;
        }
        size_t n = l->heard_count < max ? l->heard_count : max;
        memcpy(out, l->heard, n * sizeof(*out));
        qsort(out, n, sizeof(*out), compare_heard);
        return n;
    }
    return 0;
}
