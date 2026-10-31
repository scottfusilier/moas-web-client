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
    char logger[CAPS_OASP_NOTICE]; /* a logger's message for it; "" if none */
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
    bool show_txrx; /* ask for TR in Hello, and show it */
    struct radio radios[CAPS_OASP_RADIOS];
    size_t radio_count;

    struct oasp_client *client;
    bool connected;
    struct oasp_message server;
    char last_error[CAPS_FREQ_ERROR];
    /*
     * The server's last Text, kept across reconnecting so that a refusal
     * stays on the page; one asking this client to stop goes once a server
     * accepts it.
     */
    char notice[CAPS_OASP_NOTICE];
    bool notice_exit;
    /*
     * Whether any radio has reported receive or transmit on this
     * connection.  A server starts every radio as inhibited, and only a
     * switch with IndicateTR changes that; until then an inhibited radio is
     * shown as "unreported" rather than as a fault.
     */
    bool tr_reported;
};

/* After a server refuses this client, how long before asking again. */
#define REFUSED_RETRY_MS 60000

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
        l->radios[i].logger[0] = '\0';
    }
    l->tr_reported = false;
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
    struct app_link *l = arg;
    struct radio *r;

    switch (msg->kind) {
    case OASP_SERVER:
        oasp_message_take(&l->server, msg);
        /* Accepted: an earlier refusal no longer applies. */
        if (l->notice_exit) {
            l->notice[0] = '\0';
            l->notice_exit = false;
        }
        break;
    case OASP_TEXT:
        snprintf(l->notice, sizeof(l->notice), "%s", msg->u.text.text);
        l->notice_exit = msg->u.text.action == OASP_TEXT_EXIT;
        if (l->notice_exit) {
            log_warn("the MOAS server refused this client: %s",
                     l->notice[0] != '\0' ? l->notice : "(no reason given)");
            oasp_client_retry_later(client, REFUSED_RETRY_MS);
        } else {
            log_info("the MOAS server says: %s", l->notice);
        }
        break;
    case OASP_LOGGER:
        r = find_radio(l, msg->u.logger.station, msg->u.logger.radio);
        if (r != NULL) {
            snprintf(r->logger, sizeof(r->logger), "%s",
                     msg->u.logger.message);
        }
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
            if (r->tr == OASP_TR_RECEIVE || r->tr == OASP_TR_TRANSMIT) {
                l->tr_reported = true;
            }
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
                .name = "moas-web-client",
                .bidirectional = true,
                .indicate_tr = l->show_txrx,
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
    l->show_txrx = s->show_txrx;
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
                l->columns == s->columns && l->show_txrx == s->show_txrx &&
                count == l->radio_count;
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
    l->show_txrx = s->show_txrx;

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

static const struct oasp_antenna_state *antenna_state(const struct radio *r,
                                                      int antenna);
static int function_state(const struct radio *r, int function);

static const struct oasp_antenna_button *
antenna_button(const struct radio *r, int antenna)
{
    if (r->configuration.kind != OASP_CONFIGURATION) {
        return NULL;
    }
    const struct oasp_configuration *c = &r->configuration.u.configuration;
    for (size_t i = 0; i < c->antenna_count; i++) {
        if (c->antennas[i].antenna == antenna) {
            return &c->antennas[i];
        }
    }
    return NULL;
}

static const struct oasp_function_button *
function_button(const struct radio *r, int function)
{
    if (r->configuration.kind != OASP_CONFIGURATION) {
        return NULL;
    }
    const struct oasp_configuration *c = &r->configuration.u.configuration;
    for (size_t i = 0; i < c->function_count; i++) {
        if (c->functions[i].function == function) {
            return &c->functions[i];
        }
    }
    return NULL;
}

/* Adds an additive antenna if it is available, or takes it away. */
static int
toggle(int availability)
{
    return availability == OASP_SELECTED    ? OASP_CHANGE_REMOVE
           : availability == OASP_AVAILABLE ? OASP_CHANGE_ADD
                                            : OASP_CHANGE_NONE;
}

int
app_link_select(struct app_link *l, struct oasp_radio_id id, int antenna,
                bool tx, bool rx)
{
    struct radio *r = find_radio(l, id.station, id.radio);
    if (r == NULL || (!tx && !rx)) {
        return ERR_INVALID;
    }
    if (!l->connected) {
        return ERR_CLOSED;
    }
    int tx_change = tx ? OASP_CHANGE_SYSTEM : OASP_CHANGE_NONE;
    int rx_change = rx ? OASP_CHANGE_SYSTEM : OASP_CHANGE_NONE;

    const struct oasp_antenna_button *b = antenna_button(r, antenna);
    if (b != NULL && b->additive) {
        const struct oasp_antenna_state *s = antenna_state(r, antenna);
        int tx_now = s != NULL ? s->tx : OASP_UNAVAILABLE;
        int rx_now = s != NULL ? s->rx : OASP_UNAVAILABLE;
        tx_change = tx ? toggle(tx_now) : OASP_CHANGE_NONE;
        if (rx && tx_change != OASP_CHANGE_NONE) {
            /* Receive goes the same way as transmit, where it can. */
            rx_change = rx_now != OASP_UNAVAILABLE ? tx_change
                                                   : OASP_CHANGE_NONE;
        } else {
            rx_change = rx ? toggle(rx_now) : OASP_CHANGE_NONE;
        }
        if (tx_change == OASP_CHANGE_NONE && rx_change == OASP_CHANGE_NONE) {
            return 0; /* unavailable either way: nothing to ask */
        }
    }
    return oasp_client_send_antenna(l->client, id, antenna, tx_change,
                                    rx_change);
}

int
app_link_function(struct app_link *l, struct oasp_radio_id id, int function)
{
    struct radio *r = find_radio(l, id.station, id.radio);
    if (r == NULL) {
        return ERR_INVALID;
    }
    if (!l->connected) {
        return ERR_CLOSED;
    }
    int change = OASP_FUNCTION_SELECT;
    const struct oasp_function_button *f = function_button(r, function);
    if (f != NULL && !f->momentary &&
        function_state(r, function) == OASP_SELECTED)
    {
        change = OASP_FUNCTION_DESELECT; /* it is on: turn it off */
    }
    return oasp_client_send_function(l->client, id, function, change);
}

int
app_link_step(struct app_link *l, struct oasp_radio_id id, bool next)
{
    if (find_radio(l, id.station, id.radio) == NULL) {
        return ERR_INVALID;
    }
    if (!l->connected) {
        return ERR_CLOSED;
    }
    int change = next ? OASP_CHANGE_NEXT : OASP_CHANGE_PREVIOUS;
    return oasp_client_send_antenna(l->client, id, 0, change, change);
}

int
app_link_rx_same_as_tx(struct app_link *l, struct oasp_radio_id id)
{
    struct radio *r = find_radio(l, id.station, id.radio);
    if (r == NULL) {
        return ERR_INVALID;
    }
    if (!l->connected) {
        return ERR_CLOSED;
    }
    if (r->state.kind != OASP_STATE ||
        r->state.u.state.tx_short_name[0] == '\0')
    {
        return ERR_AGAIN; /* no transmit antenna to follow yet */
    }
    /* Transmit stays as it is; receive takes the same antenna. */
    return oasp_client_send_antenna(l->client, id, r->state.u.state.tx_id,
                                    OASP_CHANGE_ALL, OASP_CHANGE_SAME_AS_TX);
}

int
app_link_send_logger(struct app_link *l, struct oasp_radio_id id,
                     const char *text)
{
    if (find_radio(l, id.station, id.radio) == NULL) {
        return ERR_INVALID;
    }
    if (!l->connected) {
        return ERR_CLOSED;
    }
    return oasp_client_send_logger(l->client, id, text);
}

bool
app_link_in_band(const struct app_link *l, struct oasp_radio_id id, int khz)
{
    const struct radio *r = find_radio((struct app_link *)(uintptr_t)l,
                                       id.station, id.radio);
    if (r == NULL || r->configuration.kind != OASP_CONFIGURATION) {
        return false;
    }
    const struct oasp_configuration *c = &r->configuration.u.configuration;
    return c->upper_khz > 0 && khz >= c->lower_khz && khz <= c->upper_khz;
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
            !add_int(b, "state", 0) ||
            cJSON_AddBoolToObject(b, "additive", a->additive) == NULL)
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
            !add_int(b, "state", function_state(r, f->function)) ||
            cJSON_AddBoolToObject(b, "momentary", f->momentary) == NULL)
        {
            return false;
        }
    }
    return true;
}

/*
 * The radio's transmit/receive state as the page shows it: "" if not
 * asked for or not known; "unreported" for an inhibited radio while no
 * radio on the connection has reported receive or transmit, since the
 * switch then probably does not report them at all.
 */
static const char *
keying(const struct app_link *l, const struct radio *r)
{
    if (!l->show_txrx || !r->have_tr) {
        return "";
    }
    if (r->tr == OASP_TR_INHIBITED && !l->tr_reported) {
        return "unreported";
    }
    return oasp_tr_name(r->tr);
}

static bool
add_radio(cJSON *radios, const struct app_link *l, const struct radio *r)
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
           add_text(radio, "keying", keying(l, r)) &&
           add_text(radio, "logger", r->logger) && add_buttons(radio, r);
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
    if (ok && l->notice[0] != '\0') {
        ok = add_text(root, "notice", l->notice) &&
             cJSON_AddBoolToObject(root, "refused", l->notice_exit) != NULL;
    }
    ok = ok && add_int(root, "rows", srv != NULL ? srv->rows : l->rows) &&
         add_int(root, "columns", srv != NULL ? srv->columns : l->columns);
    cJSON *radios = ok ? cJSON_AddArrayToObject(root, "radios") : NULL;
    ok = radios != NULL;
    for (size_t i = 0; ok && i < l->radio_count; i++) {
        ok = add_radio(radios, l, &l->radios[i]);
    }
    int err = ok ? json_write(out, root) : ERR_NOMEM;
    cJSON_Delete(root);
    return err;
}
