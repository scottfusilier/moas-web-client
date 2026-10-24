/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The "rigctld" source type: reads one radio's frequency by polling hamlib's
 * rigctld over TCP.  Run one rigctld per radio, each on its own port.
 * Settings:
 *
 *   Address           where rigctld listens (default 127.0.0.1:4532)
 *   PollMilliseconds  time between readings (default 250, at least 50)
 *   TimeoutSeconds    readings older than this are stale (default 2)
 *
 * After any failure the connection is closed, the failure is shown, and the
 * source connects again 2 seconds later.
 */

#ifndef MOAS_FREQ_RIGCTLD_H
#define MOAS_FREQ_RIGCTLD_H

#include "core/common.h"
#include "core/loop.h"
#include "freq/source.h"
#include "platform/net.h"

#define FREQ_RIGCTLD_DEFAULT_ADDRESS "127.0.0.1:4532"

extern const struct freq_source_ops freq_rigctld_ops;

/*
 * A one-off reading, for checking an address before using it (the page's
 * Test button).  done is called once, with err 0 and the frequency, or an
 * error and a description; the handle is invalid after that.
 */
struct freq_rigctld_test;
typedef void freq_rigctld_test_fn(int err, int khz, const char *message,
                                  void *arg);

int freq_rigctld_test(struct loop *loop, const struct plat_addr *addr,
                      freq_rigctld_test_fn *done, void *arg,
                      struct freq_rigctld_test **out);

/* Abandons a test that has not finished; done is not called. */
void freq_rigctld_test_cancel(struct freq_rigctld_test *test);

#endif /* MOAS_FREQ_RIGCTLD_H */
