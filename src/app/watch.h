/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moasweb -watch: connects to the MOAS server as one radio and prints
 * everything the server reports, including TR changes, until stopped.  It
 * is read-only: it sends Hello and nothing else, so it never changes what
 * the switch does.  (Even reporting a frequency could switch relays.)
 *
 * Because it reports no frequency, a radio whose frequency the server does
 * not know yet gets no configuration; watch mode says so.  Normally moasweb
 * or another client has reported it already.
 */

#ifndef MOAS_APP_WATCH_H
#define MOAS_APP_WATCH_H

#include "app/args.h"
#include "core/loop.h"
#include "oasp/oasp.h"
#include "platform/net.h"

struct app_watch;

struct app_watch_options {
    struct plat_addr server;
    struct oasp_radio_id radio;
    int rows;
    int columns;
    uint32_t quiet_ms;     /* 0: 1.5 s; the pause that ends a reply */
    uint32_t retry_min_ms; /* 0: the client's default */
};

/* Receives each piece of text watch mode prints. */
typedef void app_output_fn(const char *text, void *arg);

int app_watch_create(struct loop *loop,
                     const struct app_watch_options *options,
                     app_output_fn *output, void *arg, struct app_watch **out);
void app_watch_destroy(struct app_watch *watch);

/*
 * Runs watch mode for the command line: prints to standard output and
 * stops on Ctrl+C.  Returns the program's exit status.
 */
int app_watch_run(const struct app_args *args);

#endif /* MOAS_APP_WATCH_H */
