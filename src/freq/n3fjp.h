/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The "n3fjp" source type: reads the frequency an N3FJP program (such as
 * Amateur Contact Log) shows, through its TCP API, which is turned on in
 * the program's Settings > Application Program Interface.  Settings:
 *
 *   Address           where the API listens (default 127.0.0.1:1100)
 *   PollMilliseconds  time between requests (default 2000, at least 1000)
 *   TimeoutSeconds    readings older than this are stale (default and
 *                     least: the poll plus 3 seconds)
 *
 * The program sends the frequency by itself as soon as it changes, which
 * is how changes usually arrive; the poll is a check that the program is
 * still answering, and catches a change that was missed.  The program also
 * repeats every reply to every program connected to it.  Each of these
 * counts as a reading.  Anything else it sends is ignored.  After any
 * failure the connection is closed, the failure is shown, and the source
 * connects again 2 seconds later.
 */

#ifndef MOAS_FREQ_N3FJP_H
#define MOAS_FREQ_N3FJP_H

#include "core/common.h"
#include "core/loop.h"
#include "freq/source.h"
#include "platform/net.h"

#define FREQ_N3FJP_DEFAULT_ADDRESS "127.0.0.1:1100"

extern const struct freq_source_ops freq_n3fjp_ops;

/*
 * A one-off reading, for checking an address before using it (the page's
 * Test button).  done is called once, with err 0 and the frequency, or an
 * error and a description; the handle is invalid after that.
 */
struct freq_n3fjp_test;
typedef void freq_n3fjp_test_fn(int err, int khz, const char *message,
                                void *arg);

int freq_n3fjp_test(struct loop *loop, const struct plat_addr *addr,
                    freq_n3fjp_test_fn *done, void *arg,
                    struct freq_n3fjp_test **out);

/* Abandons a test that has not finished; done is not called. */
void freq_n3fjp_test_cancel(struct freq_n3fjp_test *test);

#endif /* MOAS_FREQ_N3FJP_H */
