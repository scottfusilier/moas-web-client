/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The connection to the MOAS server as the page sees it.  It keeps the
 * connection open (reconnecting when it drops), holds the latest of what
 * the server has said for each radio, and remembers each radio's frequency
 * so that it can be sent again after reconnecting: a restarted server has
 * forgotten it, and reporting a frequency the server already has does not
 * disturb the antenna selection.
 *
 * It knows no antenna rules: what may be selected is whatever the server's
 * last State said.  It only picks the kind of request a click makes from
 * what the server said about the button (additive, momentary).  It also
 * keeps the server's last Text, so that a refusal stays on the page.
 */

#ifndef MOAS_APP_LINK_H
#define MOAS_APP_LINK_H

#include "app/settings.h"
#include "core/buf.h"
#include "core/loop.h"
#include "freq/tracker.h"
#include "oasp/oasp.h"

struct app_link;

/* Called whenever the snapshot would be different. */
typedef void app_changed_fn(void *arg);

int app_link_create(struct loop *loop, const struct app_settings *s,
                    uint32_t retry_ms, app_changed_fn *changed, void *arg,
                    struct app_link **out);
void app_link_destroy(struct app_link *link);

/*
 * Applies new settings: if the server or what Hello asks for changed, the
 * connection is dropped and made again at once.  Frequencies are kept for
 * the radios still served, and sent again on connecting.
 */
int app_link_reconfigure(struct app_link *link, const struct app_settings *s);

bool app_link_has_radio(const struct app_link *link, struct oasp_radio_id id);
bool app_link_connected(const struct app_link *link);

/*
 * Reports a radio's frequency; origin is shown on the page (a source's
 * name, or "manual").  It is remembered, and sent again on reconnecting,
 * even when there is no connection now (ERR_CLOSED).
 */
int app_link_set_frequency(struct app_link *link, struct oasp_radio_id id,
                           int khz, const char *origin);

/*
 * Requests to the server for a radio.  Each returns ERR_INVALID for a radio
 * this client does not serve, and ERR_CLOSED if not connected.
 */

/*
 * A click on an antenna, for transmit, receive or both.  An antenna in an
 * additive system is added if it is available and taken away if it is
 * selected; any other replaces the selection in its system.
 */
int app_link_select(struct app_link *link, struct oasp_radio_id id,
                    int antenna, bool tx, bool rx);

/*
 * A click on a function: a momentary one acts once; any other is turned
 * off if it is on, and on otherwise.
 */
int app_link_function(struct app_link *link, struct oasp_radio_id id,
                      int function);

/* The next (or previous) antenna in the band's relative antenna list. */
int app_link_step(struct app_link *link, struct oasp_radio_id id, bool next);

/*
 * Receive on the antenna transmit uses.  ERR_AGAIN if the server has not
 * said what transmit uses yet.
 */
int app_link_rx_same_as_tx(struct app_link *link, struct oasp_radio_id id);

/*
 * A logger's message for the radio, for the server to show the other
 * clients serving it.  Text that is not printable ASCII is not sent
 * (ERR_INVALID).
 */
int app_link_send_logger(struct app_link *link, struct oasp_radio_id id,
                         const char *text);

/*
 * Whether khz is within the band the server last sent buttons for; false
 * if it has sent none.
 */
bool app_link_in_band(const struct app_link *link, struct oasp_radio_id id,
                      int khz);

/* Shows a radio's frequency sources in snapshots (NULL: none). */
void app_link_track(struct app_link *link, struct oasp_radio_id id,
                    struct freq_tracker *tracker);
void app_link_untrack_all(struct app_link *link);

/* Something shown in the snapshot changed outside the link. */
void app_link_notify(struct app_link *link);

/* Everything the page needs to draw itself, as JSON, appended to out. */
int app_link_snapshot(const struct app_link *link, struct buf *out);

#endif /* MOAS_APP_LINK_H */
