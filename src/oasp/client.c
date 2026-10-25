/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "core/caps.h"
#include "core/log.h"
#include "oasp/client.h"
#include "oasp/frame.h"

#define DEFAULT_CONNECT_TIMEOUT_MS 5000u
#define DEFAULT_RETRY_MIN_MS 2000u
#define DEFAULT_RETRY_MAX_MS 30000u

/* Bytes read from the socket at a time. */
#define READ_CHUNK 4096

/* Received bytes waiting to be split: one whole message plus a read. */
#define INPUT_MAX (CAPS_OASP_MESSAGE + READ_CHUNK)

enum client_state {
    WAITING,    /* for the next attempt */
    CONNECTING, /* connect() in progress */
    CONNECTED,
};

struct oasp_client {
    struct loop *loop;
    struct oasp_client_events events;
    void *arg;

    struct plat_addr server;
    uint32_t connect_timeout_ms;
    uint32_t retry_min_ms;
    uint32_t retry_max_ms;

    /* Our own copy of the hello. */
    struct oasp_hello hello;
    char name[CAPS_OASP_TEXT];
    struct oasp_radio_id radios[CAPS_OASP_RADIOS];

    enum client_state state;
    plat_socket sock;
    struct loop_timer timer; /* connect time limit, or the retry pause */
    uint32_t retry_ms;       /* the pause after the next failure */
    struct buf in;
    struct buf out;
    struct oasp_framer framer;
    int sequence; /* of the last message queued */

    /*
     * Callbacks may destroy the client.  While one runs, busy is non-zero
     * and destroying only marks the client dead; it is freed once the
     * callback returns.
     */
    int busy;
    bool dead;
};

static void on_io(struct loop *loop, plat_socket sock, int events, void *arg);
static void on_timer(struct loop *loop, void *arg);

static void
free_client(struct oasp_client *c)
{
    buf_fini(&c->in);
    buf_fini(&c->out);
    free(c);
}

/*
 * Call after every callback.  True if the callback destroyed the client,
 * which the caller must then not touch again.
 */
static bool
after_callback(struct oasp_client *c)
{
    c->busy--;
    if (!c->dead) {
        return false;
    }
    if (c->busy == 0) {
        free_client(c);
    }
    return true;
}

static void
close_socket(struct oasp_client *c)
{
    if (c->sock != PLAT_INVALID_SOCKET) {
        loop_unwatch(c->loop, c->sock);
        plat_socket_close(c->sock);
        c->sock = PLAT_INVALID_SOCKET;
    }
}

/*
 * Drops the connection and schedules the next attempt.  Returns true if the
 * disconnected callback destroyed c, which the caller must then not touch.
 */
static bool
fail(struct oasp_client *c, int err)
{
    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
    c->state = WAITING;
    buf_clear(&c->in);
    buf_clear(&c->out);

    uint32_t pause = c->retry_ms;
    loop_timer_start(c->loop, &c->timer, pause, 0);
    c->retry_ms = pause > c->retry_max_ms / 2 ? c->retry_max_ms : pause * 2;

    if (c->events.disconnected == NULL) {
        return false;
    }
    c->busy++;
    c->events.disconnected(c, err, pause, c->arg);
    return after_callback(c);
}

/* Watches the socket for what it needs: reading, and writing if queued. */
static void
update_watch(struct oasp_client *c)
{
    int events = LOOP_READ | (c->out.len > 0 ? LOOP_WRITE : 0);
    if (loop_watch(c->loop, c->sock, events, on_io, c) != 0) {
        /* The loop is full; there is no way to wait for this socket. */
        log_error("MOAS server connection: too many sockets to watch");
    }
}

static void
start_connect(struct oasp_client *c)
{
    int err = plat_tcp_connect(&c->server, &c->sock);
    if (err == 0) {
        err = loop_watch(c->loop, c->sock, LOOP_WRITE, on_io, c);
    }
    if (err != 0) {
        fail(c, err);
        return;
    }
    c->state = CONNECTING;
    loop_timer_start(c->loop, &c->timer, c->connect_timeout_ms, 0);
}

/* Queues one message built by encode; on failure nothing is queued. */
static int
queue(struct oasp_client *c, int err, size_t before, int sequence)
{
    if (err != 0) {
        buf_truncate(&c->out, before);
        return err;
    }
    c->sequence = sequence;
    update_watch(c);
    return 0;
}

/* The connect finished; send Hello.  May free c. */
static void
on_connected(struct oasp_client *c)
{
    loop_timer_stop(c->loop, &c->timer);
    c->state = CONNECTED;
    c->sequence = 0;
    oasp_framer_reset(&c->framer);
    int err = oasp_encode_hello(&c->out, 1, &c->hello);
    err = queue(c, err, 0, 1);
    if (err != 0) {
        fail(c, err);
        return;
    }
    if (c->events.connected != NULL) {
        c->busy++;
        c->events.connected(c, c->arg);
        after_callback(c);
    }
}

/* Sends what is queued. */
static int
flush(struct oasp_client *c)
{
    while (c->out.len > 0) {
        size_t sent = 0;
        int err = plat_send(c->sock, c->out.data, c->out.len, &sent);
        if (err == ERR_AGAIN) {
            break;
        }
        if (err != 0) {
            return err;
        }
        buf_consume(&c->out, sent);
    }
    update_watch(c);
    return 0;
}

/*
 * Splits what has been received into messages and hands each one over.
 * Returns true if c was freed by a callback.
 */
static bool
deliver(struct oasp_client *c)
{
    for (;;) {
        size_t start;
        size_t end;
        int err = oasp_frame_next(&c->framer, c->in.data, c->in.len, &start,
                                  &end);
        if (err == ERR_AGAIN) {
            return false;
        }
        if (err != 0) {
            log_warn("MOAS server sent data that is not a message (%s); "
                     "reconnecting",
                     err_str(err));
            return fail(c, err);
        }

        struct oasp_message msg;
        err = oasp_decode(str_make(c->in.data + start, end - start), &msg);
        buf_consume(&c->in, end);
        oasp_framer_reset(&c->framer);
        if (err != 0) {
            log_warn("ignoring a %s message from the MOAS server: %s",
                     msg.name[0] != '\0' ? msg.name : "malformed",
                     err_str(err));
            continue;
        }
        /* The server answers: the next failure starts a fresh back-off. */
        c->retry_ms = c->retry_min_ms;
        if (c->events.message != NULL) {
            c->busy++;
            c->events.message(c, &msg, c->arg);
            oasp_message_free(&msg);
            if (after_callback(c)) {
                return true;
            }
            /* The callback may have disconnected the client. */
            if (c->state != CONNECTED) {
                return false;
            }
        } else {
            oasp_message_free(&msg);
        }
    }
}

static void
on_io(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    struct oasp_client *c = arg;

    if (c->state == CONNECTING) {
        int err = plat_socket_error(c->sock);
        if (err != 0) {
            fail(c, err);
        } else {
            on_connected(c);
        }
        return;
    }
    if (c->state != CONNECTED) {
        return;
    }

    if ((events & LOOP_WRITE) != 0) {
        int err = flush(c);
        if (err != 0) {
            fail(c, err);
            return;
        }
    }
    if ((events & LOOP_READ) != 0) {
        int err = buf_reserve(&c->in, READ_CHUNK);
        size_t got = 0;
        if (err == 0) {
            err = plat_recv(c->sock, buf_tail(&c->in), READ_CHUNK, &got);
        }
        if (err == ERR_AGAIN) {
            return;
        }
        if (err != 0) {
            fail(c, err);
            return;
        }
        buf_commit(&c->in, got);
        deliver(c);
    }
}

static void
on_timer(struct loop *loop, void *arg)
{
    (void)loop;
    struct oasp_client *c = arg;

    if (c->state == CONNECTING) {
        fail(c, ERR_TIMEOUT);
    } else if (c->state == WAITING) {
        start_connect(c);
    }
}

int
oasp_client_create(struct loop *loop,
                   const struct oasp_client_options *options,
                   const struct oasp_client_events *events, void *arg,
                   struct oasp_client **out)
{
    const struct oasp_hello *h = &options->hello;
    if (h->name == NULL || h->radio_count > CAPS_OASP_RADIOS ||
        strlen(h->name) >= CAPS_OASP_TEXT)
    {
        return ERR_INVALID;
    }
    struct oasp_client *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return ERR_NOMEM;
    }
    c->loop = loop;
    if (events != NULL) {
        c->events = *events;
    }
    c->arg = arg;
    c->server = options->server;
    c->connect_timeout_ms = options->connect_timeout_ms != 0
                                ? options->connect_timeout_ms
                                : DEFAULT_CONNECT_TIMEOUT_MS;
    c->retry_min_ms = options->retry_min_ms != 0 ? options->retry_min_ms
                                                 : DEFAULT_RETRY_MIN_MS;
    c->retry_max_ms = options->retry_max_ms != 0 ? options->retry_max_ms
                                                 : DEFAULT_RETRY_MAX_MS;
    if (c->retry_max_ms < c->retry_min_ms) {
        c->retry_max_ms = c->retry_min_ms;
    }
    c->retry_ms = c->retry_min_ms;

    c->hello = *h;
    memcpy(c->name, h->name, strlen(h->name) + 1);
    c->hello.name = c->name;
    if (h->radio_count > 0) {
        memcpy(c->radios, h->radios, h->radio_count * sizeof(c->radios[0]));
    }
    c->hello.radios = c->radios;

    c->sock = PLAT_INVALID_SOCKET;
    buf_init(&c->in, INPUT_MAX);
    buf_init(&c->out, CAPS_OASP_SEND_QUEUE);
    loop_timer_init(&c->timer, on_timer, c);

    /*
     * Connect on the loop's first pass rather than here, so that no
     * callback can run (and destroy the client) before the caller has it.
     */
    c->state = WAITING;
    loop_timer_start(loop, &c->timer, 0, 0);
    *out = c;
    return 0;
}

void
oasp_client_destroy(struct oasp_client *c)
{
    if (c == NULL || c->dead) {
        return;
    }
    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
    if (c->busy > 0) {
        c->dead = true; /* freed when the callback returns */
        return;
    }
    free_client(c);
}

bool
oasp_client_is_connected(const struct oasp_client *c)
{
    return c->state == CONNECTED;
}

int
oasp_client_send_radio(struct oasp_client *c, struct oasp_radio_id id, int khz)
{
    if (c->state != CONNECTED) {
        return ERR_CLOSED;
    }
    size_t before = c->out.len;
    int sequence = c->sequence + 1;
    int err = oasp_encode_radio(&c->out, sequence, id, khz);
    return queue(c, err, before, sequence);
}

int
oasp_client_send_antenna(struct oasp_client *c, struct oasp_radio_id id,
                         int antenna, int tx_change, int rx_change)
{
    if (c->state != CONNECTED) {
        return ERR_CLOSED;
    }
    size_t before = c->out.len;
    int sequence = c->sequence + 1;
    int err = oasp_encode_antenna(&c->out, sequence, id, antenna, tx_change,
                                  rx_change);
    return queue(c, err, before, sequence);
}

int
oasp_client_send_function(struct oasp_client *c, struct oasp_radio_id id,
                          int function, int change)
{
    if (c->state != CONNECTED) {
        return ERR_CLOSED;
    }
    size_t before = c->out.len;
    int sequence = c->sequence + 1;
    int err = oasp_encode_function(&c->out, sequence, id, function, change);
    return queue(c, err, before, sequence);
}

int
oasp_client_send_logger(struct oasp_client *c, struct oasp_radio_id id,
                        const char *message)
{
    if (c->state != CONNECTED) {
        return ERR_CLOSED;
    }
    size_t before = c->out.len;
    int sequence = c->sequence + 1;
    int err = oasp_encode_logger(&c->out, sequence, id, message);
    return queue(c, err, before, sequence);
}

void
oasp_client_retry_later(struct oasp_client *c, uint32_t ms)
{
    if (ms > c->retry_ms) {
        c->retry_ms = ms;
    }
}
