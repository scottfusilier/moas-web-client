/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/link.h"
#include "core/json.h"
#include "core/log.h"
#include "oasp/client.h"

/* One radio this client serves. */
struct radio {
    struct oasp_radio_id id;
    int khz; /* the last frequency reported; 0 if never */
    char origin[CAPS_FREQ_NAME];
    struct freq_tracker *tracker;
    /* What the server last said about it; OASP_NONE if nothing. */
    struct oasp_message configuration;
    struct oasp_message state;
    int tr;
    bool have_tr;
};

struct app_link {
    struct loop *loop;
    uint32_t retry_ms;
    app_changed_fn *changed;
    void *arg;

    char address[PLAT_ADDR_TEXT_MAX];
    struct plat_addr addr;
    int rows; /* asked for in Hello */
    int columns;
    struct radio radios[CAPS_OASP_RADIOS];
    size_t radio_count;

    struct oasp_client *client;
    bool connected;
    struct oasp_message server;
    char last_error[CAPS_FREQ_ERROR];
};

static void
changed(struct app_link *l)
{
    if (l->changed != NULL) {
        l->changed(l->arg);
    }
}

static struct radio *
find_radio(struct app_link *l, int station, int radio)
{
    for (size_t i = 0; i < l->radio_count; i++) {
        if (l->radios[i].id.station == station &&
            l->radios[i].id.radio == radio)
        {
            return &l->radios[i];
        }
    }
    return NULL;
}

/* Forgets what the server said, so that nothing stale is shown. */
static void
forget_server(struct app_link *l)
{
    oasp_message_free(&l->server);
    for (size_t i = 0; i < l->radio_count; i++) {
        oasp_message_free(&l->radios[i].configuration);
        oasp_message_free(&l->radios[i].state);
        l->radios[i].have_tr = false;
    }
}

static void
on_connected(struct oasp_client *client, void *arg)
{
    struct app_link *l = arg;
    l->connected = true;
    l->last_error[0] = '\0';
    for (size_t i = 0; i < l->radio_count; i++) {
        const struct radio *r = &l->radios[i];
        if (r->khz > 0) {
            oasp_client_send_radio(client, r->id, r->khz);
        }
    }
    changed(l);
}

static void
on_message(struct oasp_client *client, struct oasp_message *msg, void *arg)
{
    (void)client;
    struct app_link *l = arg;
    struct radio *r;

    switch (msg->kind) {
    case OASP_SERVER:
        oasp_message_take(&l->server, msg);
        break;
    case OASP_CONFIGURATION:
        r = find_radio(l, msg->u.configuration.station,
                       msg->u.configuration.radio);
        if (r != NULL) {
            oasp_message_take(&r->configuration, msg);
        }
        break;
    case OASP_STATE:
        r = find_radio(l, msg->u.state.station, msg->u.state.radio);
        if (r != NULL) {
            oasp_message_take(&r->state, msg);
        }
        break;
    case OASP_TR:
        r = find_radio(l, msg->u.tr.station, msg->u.tr.radio);
        if (r != NULL) {
            r->tr = msg->u.tr.tr;
            r->have_tr = true;
        }
        break;
    case OASP_UNKNOWN:
        log_info("unrecognised \"%s\" message from the MOAS server",
                 msg->name);
        return;
    default:
        return;
    }
    changed(l);
}

static void
on_disconnected(struct oasp_client *client, int err, uint32_t retry_ms,
                void *arg)
{
    (void)client;
    (void)retry_ms;
    struct app_link *l = arg;
    l->connected = false;
    forget_server(l);
    snprintf(l->last_error, sizeof(l->last_error), "%s", err_str(err));
    changed(l);
}

/* A new client for the current address and radios. */
static int
connect_client(struct app_link *l, int station)
{
    (void)station;
    struct oasp_radio_id ids[CAPS_OASP_RADIOS];
    for (size_t i = 0; i < l->radio_count; i++) {
        ids[i] = l->radios[i].id;
    }
    const struct oasp_client_options options = {
        .server = l->addr,
        .hello =
            {
                .name = "moasweb",
                .bidirectional = true,
                .indicate_tr = true,
                .radios = ids,
                .radio_count = l->radio_count,
                .rows = l->rows,
                .columns = l->columns,
            },
        .retry_min_ms = l->retry_ms,
    };
    const struct oasp_client_events events = {
        .connected = on_connected,
        .message = on_message,
        .disconnected = on_disconnected,
    };
    return oasp_client_create(l->loop, &options, &events, l, &l->client);
}

/* The radios settings ask for, in order. */
static void
radios_from(const struct app_settings *s, struct oasp_radio_id *ids,
            size_t *count)
{
    *count = s->radio_count;
    for (size_t i = 0; i < s->radio_count; i++) {
        ids[i].station = s->station;
        ids[i].radio = s->radios[i].radio;
    }
}

int
app_link_create(struct loop *loop, const struct app_settings *s,
                uint32_t retry_ms, app_changed_fn *changed_fn, void *arg,
                struct app_link **out)
{
    struct app_link *l = calloc(1, sizeof(*l));
    if (l == NULL) {
        return ERR_NOMEM;
    }
    l->loop = loop;
    l->retry_ms = retry_ms;
    l->changed = changed_fn;
    l->arg = arg;
    snprintf(l->address, sizeof(l->address), "%s", s->server);
    l->addr = s->server_addr;
    l->rows = s->rows;
    l->columns = s->columns;
    struct oasp_radio_id ids[CAPS_OASP_RADIOS];
    radios_from(s, ids, &l->radio_count);
    for (size_t i = 0; i < l->radio_count; i++) {
        l->radios[i].id = ids[i];
    }
    int err = connect_client(l, s->station);
    if (err != 0) {
        free(l);
        return err;
    }
    *out = l;
    return 0;
}

void
app_link_destroy(struct app_link *l)
{
    if (l == NULL) {
        return;
    }
    oasp_client_destroy(l->client);
    forget_server(l);
    free(l);
}

int
app_link_reconfigure(struct app_link *l, const struct app_settings *s)
{
    struct oasp_radio_id ids[CAPS_OASP_RADIOS];
    size_t count;
    radios_from(s, ids, &count);

    bool same = strcmp(l->address, s->server) == 0 && l->rows == s->rows &&
                l->columns == s->columns && count == l->radio_count;
    for (size_t i = 0; same && i < count; i++) {
        same = ids[i].station == l->radios[i].id.station &&
               ids[i].radio == l->radios[i].id.radio;
    }
    if (same) {
        return 0;
    }

    /* A deliberate disconnect: no error is shown for it. */
    oasp_client_destroy(l->client);
    l->client = NULL;
    l->connected = false;
    l->last_error[0] = '\0';
    forget_server(l);

    /* Keep the frequency (and sources) of the radios still served. */
    struct radio next[CAPS_OASP_RADIOS];
    memset(next, 0, sizeof(next));
    for (size_t i = 0; i < count; i++) {
        next[i].id = ids[i];
        struct radio *old = find_radio(l, ids[i].station, ids[i].radio);
        if (old != NULL) {
            next[i].khz = old->khz;
            memcpy(next[i].origin, old->origin, sizeof(next[i].origin));
            next[i].tracker = old->tracker;
        }
    }
    memcpy(l->radios, next, sizeof(next));
    l->radio_count = count;
    snprintf(l->address, sizeof(l->address), "%s", s->server);
    l->addr = s->server_addr;
    l->rows = s->rows;
    l->columns = s->columns;

    int err = connect_client(l, s->station);
    changed(l);
    return err;
}

bool
app_link_has_radio(const struct app_link *l, struct oasp_radio_id id)
{
    return find_radio((struct app_link *)(uintptr_t)l, id.station, id.radio) !=
           NULL;
}

bool
app_link_connected(const struct app_link *l)
{
    return l->connected;
}

int
app_link_set_frequency(struct app_link *l, struct oasp_radio_id id, int khz,
                       const char *origin)
{
    struct radio *r = find_radio(l, id.station, id.radio);
    if (r == NULL) {
        return ERR_INVALID;
    }
    r->khz = khz;
    snprintf(r->origin, sizeof(r->origin), "%s", origin);
    changed(l);
    if (!l->connected) {
        return ERR_CLOSED;
    }
    return oasp_client_send_radio(l->client, id, khz);
}

int
app_link_select(struct app_link *l, struct oasp_radio_id id, int antenna,
                bool tx, bool rx)
{
    if (!l->connected) {
        return ERR_CLOSED;
    }
    return oasp_client_send_antenna(l->client, id, antenna, tx, rx);
}

int
app_link_function(struct app_link *l, struct oasp_radio_id id, int function)
{
    if (!l->connected) {
        return ERR_CLOSED;
    }
    return oasp_client_send_function(l->client, id, function);
}

void
app_link_track(struct app_link *l, struct oasp_radio_id id,
               struct freq_tracker *tracker)
{
    struct radio *r = find_radio(l, id.station, id.radio);
    if (r != NULL) {
        r->tracker = tracker;
    }
    changed(l);
}

void
app_link_untrack_all(struct app_link *l)
{
    for (size_t i = 0; i < l->radio_count; i++) {
        l->radios[i].tracker = NULL;
    }
    changed(l);
}

void
app_link_notify(struct app_link *l)
{
    changed(l);
}

/* Snapshot. */

static bool
add_int(cJSON *obj, const char *key, int value)
{
    return cJSON_AddNumberToObject(obj, key, value) != NULL;
}

static bool
add_text(cJSON *obj, const char *key, const char *value)
{
    return cJSON_AddStringToObject(obj, key, value) != NULL;
}

static const struct oasp_antenna_state *
antenna_state(const struct radio *r, int antenna)
{
    if (r->state.kind != OASP_STATE) {
        return NULL;
    }
    for (size_t i = 0; i < r->state.u.state.antenna_count; i++) {
        if (r->state.u.state.antennas[i].antenna == antenna) {
            return &r->state.u.state.antennas[i];
        }
    }
    return NULL;
}

static int
function_state(const struct radio *r, int function)
{
    if (r->state.kind != OASP_STATE) {
        return 0;
    }
    for (size_t i = 0; i < r->state.u.state.function_count; i++) {
        if (r->state.u.state.functions[i].function == function) {
            return r->state.u.state.functions[i].state;
        }
    }
    return 0;
}

static bool
add_sources(cJSON *radio, const struct radio *r)
{
    cJSON *sources = cJSON_AddArrayToObject(radio, "sources");
    if (sources == NULL) {
        return false;
    }
    if (r->tracker == NULL) {
        return true;
    }
    struct freq_status st[CAPS_FREQ_SOURCES];
    size_t n = freq_tracker_status(r->tracker, st, CAPS_FREQ_SOURCES);
    for (size_t i = 0; i < n; i++) {
        cJSON *s = cJSON_CreateObject();
        bool ok = s != NULL && cJSON_AddItemToArray(sources, s) &&
                  add_text(s, "name", st[i].name) &&
                  add_text(s, "state", freq_state_name(st[i].state)) &&
                  add_int(s, "kHz", st[i].khz);
        if (ok && st[i].error[0] != '\0') {
            ok = add_text(s, "error", st[i].error);
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool
add_buttons(cJSON *radio, const struct radio *r)
{
    cJSON *buttons = cJSON_AddArrayToObject(radio, "buttons");
    if (buttons == NULL) {
        return false;
    }
    if (r->configuration.kind != OASP_CONFIGURATION) {
        return true;
    }
    const struct oasp_configuration *c = &r->configuration.u.configuration;
    for (size_t i = 0; i < c->antenna_count; i++) {
        const struct oasp_antenna_button *a = &c->antennas[i];
        /* No state for an antenna means it is unavailable. */
        const struct oasp_antenna_state *s = antenna_state(r, a->antenna);
        cJSON *b = cJSON_CreateObject();
        if (b == NULL || !cJSON_AddItemToArray(buttons, b) ||
            !add_int(b, "row", a->row) || !add_int(b, "column", a->column) ||
            !add_text(b, "kind", "antenna") || !add_int(b, "id", a->antenna) ||
            !add_text(b, "name", a->short_name) ||
            !add_text(b, "longName", a->long_name) ||
            !add_int(b, "tx", s != NULL ? s->tx : 0) ||
            !add_int(b, "rx", s != NULL ? s->rx : 0) ||
            !add_int(b, "state", 0))
        {
            return false;
        }
    }
    for (size_t i = 0; i < c->function_count; i++) {
        const struct oasp_function_button *f = &c->functions[i];
        cJSON *b = cJSON_CreateObject();
        if (b == NULL || !cJSON_AddItemToArray(buttons, b) ||
            !add_int(b, "row", f->row) || !add_int(b, "column", f->column) ||
            !add_text(b, "kind", "function") ||
            !add_int(b, "id", f->function) ||
            !add_text(b, "name", f->short_name) ||
            !add_text(b, "longName", f->long_name) || !add_int(b, "tx", 0) ||
            !add_int(b, "rx", 0) ||
            !add_int(b, "state", function_state(r, f->function)))
        {
            return false;
        }
    }
    return true;
}

static bool
add_radio(cJSON *radios, const struct radio *r)
{
    cJSON *radio = cJSON_CreateObject();
    if (radio == NULL || !cJSON_AddItemToArray(radios, radio)) {
        return false;
    }
    const struct oasp_configuration *c =
        r->configuration.kind == OASP_CONFIGURATION
            ? &r->configuration.u.configuration
            : NULL;
    const struct oasp_state *s = c != NULL && r->state.kind == OASP_STATE
                                     ? &r->state.u.state
                                     : NULL;
    return add_int(radio, "station", r->id.station) &&
           add_int(radio, "radio", r->id.radio) &&
           add_int(radio, "frequency", r->khz) &&
           add_text(radio, "origin", r->origin) && add_sources(radio, r) &&
           cJSON_AddNumberToObject(radio, "lower",
                                   c != NULL ? c->lower_khz : 0) != NULL &&
           cJSON_AddNumberToObject(radio, "upper",
                                   c != NULL ? c->upper_khz : 0) != NULL &&
           add_text(radio, "tx", s != NULL ? s->tx_short_name : "") &&
           add_text(radio, "rx", s != NULL ? s->rx_short_name : "") &&
           add_text(radio, "keying", r->have_tr ? oasp_tr_name(r->tr) : "") &&
           add_buttons(radio, r);
}

int
app_link_snapshot(const struct app_link *l, struct buf *out)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ERR_NOMEM;
    }
    const struct oasp_server *srv = l->server.kind == OASP_SERVER
                                        ? &l->server.u.server
                                        : NULL;
    /* The server reports the size of the layout it chose. */
    bool ok = cJSON_AddBoolToObject(root, "connected", l->connected) != NULL &&
              add_text(root, "address", l->address);
    if (ok && l->last_error[0] != '\0') {
        ok = add_text(root, "error", l->last_error);
    }
    if (ok && srv != NULL) {
        ok = add_text(root, "server", srv->name);
    }
    ok = ok && add_int(root, "rows", srv != NULL ? srv->rows : l->rows) &&
         add_int(root, "columns", srv != NULL ? srv->columns : l->columns);
    cJSON *radios = ok ? cJSON_AddArrayToObject(root, "radios") : NULL;
    ok = radios != NULL;
    for (size_t i = 0; ok && i < l->radio_count; i++) {
        ok = add_radio(radios, &l->radios[i]);
    }
    int err = ok ? json_write(out, root) : ERR_NOMEM;
    cJSON_Delete(root);
    return err;
}
