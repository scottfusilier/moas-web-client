/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A connection to a MOAS server, run by the event loop.  The client
 * connects, sends Hello, and passes each message the server sends to a
 * callback.  If the connection fails or ends, it tries again after a pause
 * that starts at retry_min_ms and doubles up to retry_max_ms while the
 * server stays unreachable; it returns to retry_min_ms once the server
 * answers.
 *
 * A message that cannot be decoded is logged and skipped; the connection
 * carries on.  Only a stream that cannot be split into messages any more
 * ends the connection.
 */

#ifndef MOAS_OASP_CLIENT_H
#define MOAS_OASP_CLIENT_H

#include "core/common.h"
#include "core/loop.h"
#include "oasp/oasp.h"
#include "platform/net.h"

struct oasp_client;

/*
 * Callbacks.  Any may be NULL.  From inside any of them the client may be
 * used, or destroyed.
 */
struct oasp_client_events {
    /* Connected, and Hello is on its way. */
    void (*connected)(struct oasp_client *client, void *arg);

    /*
     * A message from the server.  It is freed after the callback returns,
     * unless the callback moves it elsewhere with oasp_message_take().
     */
    void (*message)(struct oasp_client *client, struct oasp_message *msg,
                    void *arg);

    /*
     * Connecting failed, or the connection ended; err says why.  The next
     * attempt is in retry_ms.
     */
    void (*disconnected)(struct oasp_client *client, int err,
                         uint32_t retry_ms, void *arg);
};

struct oasp_client_options {
    struct plat_addr server;
    struct oasp_hello hello;     /* copied; the caller's may go away */
    uint32_t connect_timeout_ms; /* 0: 5 seconds */
    uint32_t retry_min_ms;       /* 0: 2 seconds, as the Go version */
    uint32_t retry_max_ms;       /* 0: 30 seconds */
};

/*
 * Creates the client; it starts connecting when the loop next runs, and no
 * callback runs before this returns.  ERR_INVALID if the hello does
 * not fit the limits in core/caps.h.
 */
int oasp_client_create(struct loop *loop,
                       const struct oasp_client_options *options,
                       const struct oasp_client_events *events, void *arg,
                       struct oasp_client **out);

/* Closes the connection and frees the client; safe inside its callbacks. */
void oasp_client_destroy(struct oasp_client *client);

bool oasp_client_is_connected(const struct oasp_client *client);

/*
 * Sending.  Each queues one message, sent as soon as the socket allows.
 * ERR_CLOSED if not connected (nothing is kept for a later connection), or
 * ERR_LIMIT if CAPS_OASP_SEND_QUEUE bytes are already waiting.
 */
int oasp_client_send_radio(struct oasp_client *client, struct oasp_radio_id id,
                           int khz);
int oasp_client_send_antenna(struct oasp_client *client,
                             struct oasp_radio_id id, int antenna, bool tx,
                             bool rx);
int oasp_client_send_function(struct oasp_client *client,
                              struct oasp_radio_id id, int function);

#endif /* MOAS_OASP_CLIENT_H */
