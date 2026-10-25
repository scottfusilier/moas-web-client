/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The protocol spoken between a MOAS client and a MOAS server over TCP
 * (port 12059 by default), which names itself "OASP".  Written to work with
 * MOAS Server by M1DST and K1XM, and tested against v2.0.2.  It covers the
 * messages needed for antenna selection.
 *
 * Each message is one JSON object:
 *
 *   { "Message" : "Radio", "Sequence" : 2, "Version" : 1,
 *     "Parameters" : { ... } }
 *
 * Objects follow one another on the connection with no separator (the
 * server may put whitespace between them).  Each side numbers its own
 * messages, starting at 1.  The client speaks first, with Hello.
 *
 * This header has the message types, decoding and encoding; oasp/frame.h
 * splits the byte stream into messages and oasp/client.h runs a connection.
 * None of it depends on the rest of moas-web-client, so it can be used on its
 * own.
 */

#ifndef MOAS_OASP_OASP_H
#define MOAS_OASP_OASP_H

#include "core/buf.h"
#include "core/caps.h"
#include "core/common.h"
#include "core/str.h"

#define OASP_PROTOCOL "OASP"
#define OASP_VERSION 1
#define OASP_DEFAULT_PORT 12059

/* One radio at one station. */
struct oasp_radio_id {
    int station;
    int radio;
};

/* Messages sent by the client. */

/*
 * Hello is the first message on a connection.  rows and columns are the
 * grid the client would like; the server picks the closest layout from its
 * configuration and reports the size it chose in its Server message.
 */
struct oasp_hello {
    const char *name; /* this client's name, such as "moas-web-client" */
    bool bidirectional;
    bool indicate_tr; /* ask for TR messages (sent only if true) */
    const struct oasp_radio_id *radios;
    size_t radio_count; /* at most CAPS_OASP_RADIOS */
    int rows;
    int columns;
};

/* Messages sent by the server. */

enum oasp_kind {
    OASP_NONE,          /* nothing (after oasp_message_take()) */
    OASP_SERVER,        /* the reply to Hello */
    OASP_ROTATORS,      /* the list of rotators */
    OASP_CONFIGURATION, /* the buttons for one radio's band */
    OASP_STATE,         /* what one radio uses and may select */
    OASP_TR,            /* transmit/receive status of one radio */
    OASP_UNKNOWN,       /* a message this library does not know */
};

struct oasp_server {
    char name[CAPS_OASP_TEXT];
    int rows;
    int columns;
    bool txrx;
};

/* Rotators are not handled yet; only how many there are is kept. */
struct oasp_rotators {
    size_t count;
};

struct oasp_antenna_description {
    char manufacturer[CAPS_OASP_TEXT];
    char model[CAPS_OASP_TEXT];
    char type[CAPS_OASP_TEXT];
    int elements;
    double width_3db; /* "3dBWidth", degrees */
    int lobes;
    double direction; /* degrees */
};

struct oasp_antenna_button {
    int antenna; /* the antenna's ID */
    char short_name[CAPS_OASP_TEXT];
    char long_name[CAPS_OASP_TEXT];
    struct oasp_antenna_description descriptions[CAPS_OASP_DESCRIPTIONS];
    size_t description_count;
    int row;
    int column;
};

struct oasp_function_button {
    int function; /* the function's ID */
    char short_name[CAPS_OASP_TEXT];
    char long_name[CAPS_OASP_TEXT];
    bool momentary;
    bool alternate;
    int row;
    int column;
};

/*
 * The buttons for one radio on the band it is on.  The server sends a new
 * one when the radio changes band.
 */
struct oasp_configuration {
    int station;
    int radio;
    double lower_khz;
    double upper_khz;
    struct oasp_antenna_button *antennas;
    size_t antenna_count;
    struct oasp_function_button *functions;
    size_t function_count;
};

/* The state of one antenna for transmit or for receive. */
enum oasp_availability {
    OASP_UNAVAILABLE = 0,
    OASP_AVAILABLE = 1,
    OASP_SELECTED = 2,
};

/* "unavailable", "available", "selected", or "unknown". */
const char *oasp_availability_name(int availability);

struct oasp_antenna_state {
    int antenna;
    int tx; /* an enum oasp_availability value */
    int rx;
};

struct oasp_function_state {
    int function;
    int state;
};

/*
 * What one radio is using and what it may select.  The server sends one
 * for every radio named in Hello after any change.
 */
struct oasp_state {
    int station;
    int radio;
    char tx_short_name[CAPS_OASP_TEXT];
    char tx_long_name[CAPS_OASP_TEXT];
    int tx_id;
    char rx_short_name[CAPS_OASP_TEXT];
    char rx_long_name[CAPS_OASP_TEXT];
    int rx_id;
    struct oasp_antenna_state *antennas;
    size_t antenna_count;
    struct oasp_function_state *functions;
    size_t function_count;
};

/*
 * Transmit/receive status of one radio, sent when it changes, and only if
 * the switch's IndicateTR is true in the server's configuration.  For
 * example, keying radio 1 gives 3 for radio 1, and 1 for radio 2 when an
 * Always rule inhibits it; releasing gives 2 for both.  A server with no
 * switch attached sends other values.
 */
#define OASP_TR_INHIBITED 1
#define OASP_TR_RECEIVE 2
#define OASP_TR_TRANSMIT 3

/* "inhibited", "receive", "transmit", or "" for any other value. */
const char *oasp_tr_name(int tr);

struct oasp_tr {
    int station;
    int radio;
    int tr;
};

/* A message this library does not know: its parameters, as compact JSON. */
struct oasp_unknown {
    char *parameters; /* NULL if there were none */
};

/* One decoded message from the server. */
struct oasp_message {
    enum oasp_kind kind;
    char name[CAPS_OASP_MESSAGE_NAME]; /* the "Message" field */
    int sequence;
    union {
        struct oasp_server server;
        struct oasp_rotators rotators;
        struct oasp_configuration configuration;
        struct oasp_state state;
        struct oasp_tr tr;
        struct oasp_unknown unknown;
    } u;
};

/*
 * Decodes one message (one JSON object, as found by oasp/frame.h).  On
 * success the caller frees it with oasp_message_free().
 *
 * ERR_INVALID if it is not a message at all; then msg->name is empty.  If
 * it is a message whose parameters cannot be read (a field of the wrong
 * type, a number out of range), the error is ERR_INVALID, ERR_RANGE or
 * ERR_LIMIT and msg->name says which message it was, for the log.  Either
 * way nothing needs freeing.  Unknown fields are ignored, and missing ones
 * are 0, false or "".
 */
int oasp_decode(struct str json, struct oasp_message *msg);

void oasp_message_free(struct oasp_message *msg);

/*
 * Moves a message: *dst takes over *src and what it owns, and *src is left
 * as OASP_NONE.  Frees whatever *dst held before.
 */
void oasp_message_take(struct oasp_message *dst, struct oasp_message *src);

/*
 * Encoding.  Each appends one message with the given sequence number to
 * out, as compact JSON in the field order the server's own client uses.
 */
int oasp_encode_hello(struct buf *out, int sequence,
                      const struct oasp_hello *hello);

/* Reports a radio's frequency in kHz. */
int oasp_encode_radio(struct buf *out, int sequence, struct oasp_radio_id id,
                      int khz);

/*
 * Asks for an antenna for transmit, receive, or both; receive only gives a
 * split.  ERR_INVALID if neither tx nor rx.  The server does not reply to a
 * request it will not honour; its next State shows what is selected.
 */
int oasp_encode_antenna(struct buf *out, int sequence, struct oasp_radio_id id,
                        int antenna, bool tx, bool rx);

/* Activates one of the functions in the radio's configuration. */
int oasp_encode_function(struct buf *out, int sequence,
                         struct oasp_radio_id id, int function);

#endif /* MOAS_OASP_OASP_H */
