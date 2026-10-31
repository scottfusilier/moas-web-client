/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The protocol spoken between a MOAS client and a MOAS server over TCP
 * (port 12059 by default), which names itself "OASP".  Written to work with
 * MOAS Server by M1DST and K1XM, and tested against v2.0.4.  It covers the
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
    OASP_TEXT,          /* text for the user, perhaps asking to stop */
    OASP_LOGGER,        /* a logger's message for one radio */
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
    /*
     * Part of an additive antenna system: it is added to, or taken from,
     * what is selected (OASP_CHANGE_ADD, OASP_CHANGE_REMOVE) rather than
     * replacing it.
     */
    bool additive;
    int row;
    int column;
};

/*
 * A function, or a relay group shown as one.  A momentary function acts
 * once each time it is selected.  Any other is on or off, as its state in
 * State says (OASP_SELECTED is on): it is turned on with
 * OASP_FUNCTION_SELECT and off with OASP_FUNCTION_DESELECT.
 */
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
 * Always rule inhibits it; releasing gives 2 for both.  0 means the server
 * cannot tell, because the switch is not reachable.
 */
#define OASP_TR_UNAVAILABLE 0
#define OASP_TR_INHIBITED 1
#define OASP_TR_RECEIVE 2
#define OASP_TR_TRANSMIT 3

/*
 * "unavailable", "inhibited", "receive", "transmit", or "" for any other
 * value.
 */
const char *oasp_tr_name(int tr);

struct oasp_tr {
    int station;
    int radio;
    int tr;
};

/*
 * Text for the user.  With OASP_TEXT_EXIT the server is refusing this
 * client, as when it allows only one client and another is connected; it
 * then drops the connection.
 */
#define OASP_TEXT_NONE 0
#define OASP_TEXT_EXIT 1

struct oasp_text {
    int action;
    char text[CAPS_OASP_NOTICE];
};

/*
 * A logger's message for one radio (such as the text of an F-key it is
 * sending), passed on by the server from the client that sent it.
 */
struct oasp_logger {
    int station;
    int radio;
    char message[CAPS_OASP_NOTICE];
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
        struct oasp_text text;
        struct oasp_logger logger;
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
 * out, as compact JSON with the fields in the usual order.
 */
int oasp_encode_hello(struct buf *out, int sequence,
                      const struct oasp_hello *hello);

/* Reports a radio's frequency in kHz. */
int oasp_encode_radio(struct buf *out, int sequence, struct oasp_radio_id id,
                      int khz);

/*
 * How an Antenna message changes the transmit or the receive antenna.
 */
enum oasp_antenna_change {
    OASP_CHANGE_NONE = 0, /* leave it */
    /*
     * Select this antenna in place of the others in its antenna system (for
     * an antenna in no system, in place of everything).  Selecting an
     * exclusive part of an additive system that is already selected turns
     * it off.
     */
    OASP_CHANGE_SYSTEM = 1,
    OASP_CHANGE_ALL = 2,         /* select this antenna, and only it */
    OASP_CHANGE_ADD = 3,         /* add an additive antenna */
    OASP_CHANGE_REMOVE = 4,      /* take an additive antenna away */
    OASP_CHANGE_NUMBER = 5,      /* entry "antenna" of the relative list */
    OASP_CHANGE_NEXT = 6,        /* the next antenna in the relative list */
    OASP_CHANGE_PREVIOUS = 7,    /* the previous one */
    OASP_CHANGE_SAME_AS_TX = 99, /* receive: whatever transmit uses */
};

/*
 * Asks for an antenna change for transmit, receive, or both.  The antenna
 * ID is 0 for OASP_CHANGE_NEXT and OASP_CHANGE_PREVIOUS, which do not name
 * one; OASP_CHANGE_SAME_AS_TX is for rx only.  ERR_INVALID if both changes
 * are OASP_CHANGE_NONE or a change is not one of the values above.  The
 * server does not reply to a request it will not honour; its next State
 * shows what is selected.
 */
int oasp_encode_antenna(struct buf *out, int sequence, struct oasp_radio_id id,
                        int antenna, int tx_change, int rx_change);

/* What a Function message asks of a function; see oasp_function_button. */
enum oasp_function_change {
    OASP_FUNCTION_SELECT = 1,
    OASP_FUNCTION_DESELECT = 2,
    OASP_FUNCTION_ALTERNATE = 3,
};

/* Changes one of the functions in the radio's configuration. */
int oasp_encode_function(struct buf *out, int sequence,
                         struct oasp_radio_id id, int function, int change);

/*
 * Passes a logger's message for one radio to the server, which shows it
 * to the other clients serving that radio.  Printable ASCII only, at most
 * CAPS_OASP_NOTICE - 1 bytes; ERR_INVALID otherwise.  "" clears it.
 */
int oasp_encode_logger(struct buf *out, int sequence, struct oasp_radio_id id,
                       const char *message);

#endif /* MOAS_OASP_OASP_H */
