/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A scripted stand-in for a MOAS server, for tests.  It listens on the
 * loopback interface, runs on the same event loop as the code being tested,
 * records everything a client sends, and sends whatever the test tells it
 * to.  It serves one client at a time; a new connection replaces the old.
 */

#ifndef MOAS_TESTS_FAKE_MOAS_SERVER_H
#define MOAS_TESTS_FAKE_MOAS_SERVER_H

#include "core/common.h"
#include "core/loop.h"
#include "core/str.h"
#include "platform/net.h"

struct fake_server;

typedef void fake_connect_fn(struct fake_server *server, void *arg);

int fake_server_create(struct loop *loop, struct fake_server **out);
void fake_server_destroy(struct fake_server *server);

/* Called each time a client connects. */
void fake_server_on_connect(struct fake_server *server, fake_connect_fn *fn,
                            void *arg);

/* Where to connect to it. */
const struct plat_addr *fake_server_addr(const struct fake_server *server);

/* How many connections it has accepted. */
int fake_server_accepted(const struct fake_server *server);

bool fake_server_has_client(const struct fake_server *server);

/* Everything received from the current client. */
struct str fake_server_received(const struct fake_server *server);

/* Sends to the current client (small amounts: sent in one go). */
void fake_server_send(struct fake_server *server, const char *data,
                      size_t len);
void fake_server_send_cstr(struct fake_server *server, const char *text);

/* Closes the current client's connection. */
void fake_server_drop_client(struct fake_server *server);

/*
 * Runs the loop until done(arg) is true or timeout_ms passes.  Returns
 * done(arg).
 */
bool run_until(struct loop *loop, bool (*done)(void *arg), void *arg,
               int timeout_ms);

#endif /* MOAS_TESTS_FAKE_MOAS_SERVER_H */
