/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/watch.h"
#include "core/buf.h"
#include "core/log.h"
#include "oasp/client.h"
#include "platform/clock.h"
#include "platform/signal.h"

#define DEFAULT_QUIET_MS 1500u

/* Most bytes of an unknown message's parameters shown. */
#define UNKNOWN_SHOWN 400

/* Most text produced for one event. */
#define OUTPUT_MAX (64 * 1024)

struct app_watch {
    struct loop *loop;
    struct app_watch_options options;
    app_output_fn *output;
    void *arg;
    struct oasp_client *client;
    char server_text[PLAT_ADDR_TEXT_MAX];

    /* The latest of what the server has said, for this radio. */
    struct oasp_message server;
    struct oasp_message configuration;
    struct oasp_message state;
    int tr;
    bool have_tr;

    /*
     * After connecting, the first pause in the server's messages: if no
     * configuration has come by then, say so (once per connection).
     */
    struct loop_timer quiet;
    bool quiet_reported;
};

static void
emit(struct app_watch *w, struct buf *text)
{
    if (text->len > 0) {
        w->output(buf_cstr(text), w->arg);
    }
    buf_fini(text);
}

static void
append_time(struct buf *b, bool millis)
{
    struct plat_local_time t;

    plat_local_time(&t);
    buf_appendf(b, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    if (millis) {
        buf_appendf(b, ".%03d", t.millisecond);
    }
}

static bool
is_our_radio(const struct app_watch *w, int station, int radio)
{
    return station == w->options.radio.station &&
           radio == w->options.radio.radio;
}

/* Sorts antenna buttons by row, then column. */
static int
compare_buttons(const void *a, const void *b)
{
    const struct oasp_antenna_button *x = *(
        const struct oasp_antenna_button *const *)a;
    const struct oasp_antenna_button *y = *(
        const struct oasp_antenna_button *const *)b;
    if (x->row != y->row) {
        return x->row < y->row ? -1 : 1;
    }
    if (x->column != y->column) {
        return x->column < y->column ? -1 : 1;
    }
    return 0;
}

static const struct oasp_antenna_state *
find_antenna_state(const struct oasp_state *s, int antenna)
{
    for (size_t i = 0; i < s->antenna_count; i++) {
        if (s->antennas[i].antenna == antenna) {
            return &s->antennas[i];
        }
    }
    return NULL;
}

static int
function_state(const struct oasp_state *s, int function)
{
    for (size_t i = 0; i < s->function_count; i++) {
        if (s->functions[i].function == function) {
            return s->functions[i].state;
        }
    }
    return 0;
}

/* Everything known about the radio, as a table of its buttons. */
static void
append_view(struct app_watch *w, struct buf *b)
{
    const struct oasp_radio_id id = w->options.radio;

    if (w->server.kind == OASP_SERVER) {
        const struct oasp_server *s = &w->server.u.server;
        buf_appendf(b, "Server: %s, grid %d rows x %d columns\n", s->name,
                    s->rows, s->columns);
    }
    if (w->configuration.kind != OASP_CONFIGURATION) {
        buf_appendf(b,
                    "Station %d radio %d: no configuration received.  The "
                    "server may not know this radio's frequency yet; it "
                    "sends one once a client reports it.\n",
                    id.station, id.radio);
        return;
    }
    const struct oasp_configuration *c = &w->configuration.u.configuration;
    const struct oasp_state *s = w->state.kind == OASP_STATE
                                     ? &w->state.u.state
                                     : NULL;

    buf_appendf(b, "Station %d radio %d, %g-%g kHz\n", id.station, id.radio,
                c->lower_khz, c->upper_khz);
    if (s != NULL) {
        buf_appendf(b, "  TX: %s (%d)   RX: %s (%d)", s->tx_short_name,
                    s->tx_id, s->rx_short_name, s->rx_id);
        if (w->have_tr) {
            buf_appendf(b, "   TR: %d %s", w->tr, oasp_tr_name(w->tr));
        }
        buf_append_cstr(b, "\n");
    }

    const struct oasp_antenna_button **sorted = NULL;
    if (c->antenna_count > 0) {
        sorted = calloc(c->antenna_count, sizeof(*sorted));
    }
    if (sorted != NULL) {
        for (size_t i = 0; i < c->antenna_count; i++) {
            sorted[i] = &c->antennas[i];
        }
        qsort(sorted, c->antenna_count, sizeof(*sorted), compare_buttons);
    }

    buf_appendf(b, "  %3s %3s %4s  %-18s %-12s %-12s\n", "row", "col", "id",
                "antenna", "TX", "RX");
    for (size_t i = 0; i < c->antenna_count; i++) {
        const struct oasp_antenna_button *a = sorted != NULL ? sorted[i]
                                                             : &c->antennas[i];
        const struct oasp_antenna_state *as = s != NULL ? find_antenna_state(
                                                              s, a->antenna)
                                                        : NULL;
        buf_appendf(b, "  %3d %3d %4d  %-18s %-12s %-12s\n", a->row, a->column,
                    a->antenna, a->short_name,
                    as != NULL ? oasp_availability_name(as->tx) : "-",
                    as != NULL ? oasp_availability_name(as->rx) : "-");
    }
    free(sorted);

    for (size_t i = 0; i < c->function_count; i++) {
        const struct oasp_function_button *f = &c->functions[i];
        buf_appendf(b, "  %3d %3d %4d  %-18s function, state %d\n", f->row,
                    f->column, f->function, f->short_name,
                    s != NULL ? function_state(s, f->function) : 0);
    }
}

static void
forget(struct app_watch *w)
{
    oasp_message_free(&w->server);
    oasp_message_free(&w->configuration);
    oasp_message_free(&w->state);
    w->have_tr = false;
    w->quiet_reported = false;
    loop_timer_stop(w->loop, &w->quiet);
}

static void
on_connected(struct oasp_client *client, void *arg)
{
    (void)client;
    struct app_watch *w = arg;
    struct buf b;

    buf_init(&b, OUTPUT_MAX);
    append_time(&b, false);
    buf_appendf(&b, "  connected to %s as station %d radio %d\n",
                w->server_text, w->options.radio.station,
                w->options.radio.radio);
    emit(w, &b);
    loop_timer_start(w->loop, &w->quiet, w->options.quiet_ms, 0);
}

static void
on_quiet(struct loop *loop, void *arg)
{
    (void)loop;
    struct app_watch *w = arg;
    struct buf b;

    if (w->quiet_reported) {
        return;
    }
    w->quiet_reported = true;
    buf_init(&b, OUTPUT_MAX);
    buf_append_cstr(&b, "\n== on connect\n");
    append_view(w, &b);
    emit(w, &b);
}

static void
on_message(struct oasp_client *client, struct oasp_message *msg, void *arg)
{
    (void)client;
    struct app_watch *w = arg;
    struct buf b;

    buf_init(&b, OUTPUT_MAX);
    if (!w->quiet_reported) {
        loop_timer_start(w->loop, &w->quiet, w->options.quiet_ms, 0);
    }

    switch (msg->kind) {
    case OASP_SERVER:
        oasp_message_take(&w->server, msg);
        break;
    case OASP_CONFIGURATION:
        if (is_our_radio(w, msg->u.configuration.station,
                         msg->u.configuration.radio))
        {
            oasp_message_take(&w->configuration, msg);
        }
        break;
    case OASP_STATE:
        if (is_our_radio(w, msg->u.state.station, msg->u.state.radio)) {
            oasp_message_take(&w->state, msg);
            if (w->quiet_reported) {
                buf_append_cstr(&b, "\n== update ");
                append_time(&b, false);
                buf_append_cstr(&b, "\n");
                append_view(w, &b);
            }
        }
        break;
    case OASP_TR: {
        const struct oasp_tr *t = &msg->u.tr;
        if (is_our_radio(w, t->station, t->radio)) {
            w->tr = t->tr;
            w->have_tr = true;
        }
        /*
         * Every TR is shown on its own, so that keying a radio shows even
         * when nothing else changes.
         */
        append_time(&b, true);
        buf_appendf(&b, "  TR station %d radio %d: %d %s\n", t->station,
                    t->radio, t->tr, oasp_tr_name(t->tr));
        break;
    }
    case OASP_UNKNOWN: {
        /* Shown in part, and cleaned, since it comes from the network. */
        const char *p = msg->u.unknown.parameters;
        size_t len = p != NULL ? strlen(p) : 0;
        bool cut = len > UNKNOWN_SHOWN;
        char shown[UNKNOWN_SHOWN + 1];
        str_copy_clean(str_make(p != NULL ? p : "", cut ? UNKNOWN_SHOWN : len),
                       shown, sizeof(shown));
        append_time(&b, false);
        buf_appendf(&b, "  unrecognised \"%s\" message: %s%s\n", msg->name,
                    shown, cut ? "..." : "");
        break;
    }
    default:
        break;
    }
    emit(w, &b);
}

static void
on_disconnected(struct oasp_client *client, int err, uint32_t retry_ms,
                void *arg)
{
    (void)client;
    struct app_watch *w = arg;
    struct buf b;

    forget(w);
    buf_init(&b, OUTPUT_MAX);
    append_time(&b, false);
    buf_appendf(&b, "  no connection to %s: %s; trying again in %u.%u s\n",
                w->server_text, err_str(err), (unsigned)(retry_ms / 1000u),
                (unsigned)(retry_ms % 1000u / 100u));
    emit(w, &b);
}

int
app_watch_create(struct loop *loop, const struct app_watch_options *options,
                 app_output_fn *output, void *arg, struct app_watch **out)
{
    struct app_watch *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        return ERR_NOMEM;
    }
    w->loop = loop;
    w->options = *options;
    if (w->options.quiet_ms == 0) {
        w->options.quiet_ms = DEFAULT_QUIET_MS;
    }
    w->output = output;
    w->arg = arg;
    plat_addr_format(&options->server, w->server_text);
    loop_timer_init(&w->quiet, on_quiet, w);

    const struct oasp_client_options client_options = {
        .server = options->server,
        .hello =
            {
                .name = "moas-web-client",
                .bidirectional = true,
                .indicate_tr = true,
                .radios = &w->options.radio,
                .radio_count = 1,
                .rows = options->rows,
                .columns = options->columns,
            },
        .retry_min_ms = options->retry_min_ms,
    };
    const struct oasp_client_events events = {
        .connected = on_connected,
        .message = on_message,
        .disconnected = on_disconnected,
    };
    int err = oasp_client_create(loop, &client_options, &events, w,
                                 &w->client);
    if (err != 0) {
        free(w);
        return err;
    }
    *out = w;
    return 0;
}

void
app_watch_destroy(struct app_watch *w)
{
    if (w == NULL) {
        return;
    }
    oasp_client_destroy(w->client);
    forget(w);
    free(w);
}

/* For app_watch_run(): standard output, at once. */
static void
to_stdout(const char *text, void *arg)
{
    (void)arg;
    fputs(text, stdout);
    fflush(stdout);
}

/* Ctrl+C: stop the loop. */
static void
on_stop_request(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    (void)arg;
    plat_signal_drain(sock);
    loop_stop(loop);
}

int
app_watch_run(const struct app_args *args)
{
    struct app_watch_options options = {
        .radio = {.station = args->station, .radio = args->radio},
        .rows = args->rows,
        .columns = args->columns,
    };
    if (plat_addr_parse(args->server, strlen(args->server), &options.server) !=
        0)
    {
        fprintf(stderr,
                "moas-web-client: \"%s\" is not a server address; give it as "
                "host:port with a numeric host, for example "
                "192.168.1.10:12059\n",
                args->server);
        return 2;
    }

    int err = plat_net_init();
    if (err != 0) {
        log_error("cannot start networking: %s", err_str(err));
        return 1;
    }
    struct loop *loop = NULL;
    struct app_watch *watch = NULL;
    plat_socket stop = PLAT_INVALID_SOCKET;
    bool signals = false;

    err = loop_create(&loop);
    if (err == 0) {
        err = plat_signal_open(&stop);
        signals = err == 0;
    }
    if (err == 0) {
        err = loop_watch(loop, stop, LOOP_READ, on_stop_request, NULL);
    }
    if (err == 0) {
        err = app_watch_create(loop, &options, to_stdout, NULL, &watch);
    }
    if (err == 0) {
        printf("Watching; press Ctrl+C to stop.  Nothing is sent to the "
               "server but Hello.\n");
        fflush(stdout);
        err = loop_run(loop);
    }
    if (err != 0) {
        log_error("watch mode failed: %s", err_str(err));
    }

    app_watch_destroy(watch);
    if (signals) {
        loop_unwatch(loop, stop);
        plat_signal_close();
    }
    if (loop != NULL) {
        loop_destroy(loop);
    }
    plat_net_fini();
    return err == 0 ? 0 : 1;
}
