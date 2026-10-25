/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The page and the requests it makes, served by the HTTP server.  The
 * routes, request bodies and replies are the ones page.html uses:
 *
 *   GET  /                     the page
 *   GET  /events               a snapshot now and after every change
 *   GET  /api/state            a snapshot
 *   POST /api/frequency        {station, radio, frequency}
 *   POST /api/select           {station, radio, antenna, tx, rx}
 *   POST /api/function         {station, radio, function}
 *
 * and, from this computer only:
 *
 *   GET  /api/settings         the settings file and its state
 *   POST /api/settings         {settings, version}: save and apply
 *   GET  /api/n1mm-heard       the radios N1MM and DXLog have reported
 *   POST /api/rigctld-test     {address}: read a frequency once
 */

#ifndef MOAS_APP_WEB_H
#define MOAS_APP_WEB_H

#include "app/runner.h"
#include "core/loop.h"
#include "http/http.h"

struct app_web;

/*
 * Serves the page on options->listen, for runner.  Call app_web_changed()
 * whenever the runner reports a change.
 */
int app_web_create(struct loop *loop, struct app_runner *runner,
                   const struct http_server_options *options,
                   struct app_web **out);
void app_web_destroy(struct app_web *web);

/* The snapshot changed: send it to the open pages (soon, and once). */
void app_web_changed(struct app_web *web);

/* Where the page is served (with the real port). */
const struct plat_addr *app_web_addr(const struct app_web *web);

#endif /* MOAS_APP_WEB_H */
