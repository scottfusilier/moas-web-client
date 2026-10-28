/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A stand-in for an N3FJP program's TCP API, for tests: listens on a free
 * loopback port, on the same event loop as the code being tested, and
 * answers each request line with reply (change it at any time).  It can
 * also send on its own, as N3FJP does when the frequency changes, and close
 * the way N3FJP does when it exits.
 */

#ifndef MOAS_TESTS_FAKE_N3FJP_H
#define MOAS_TESTS_FAKE_N3FJP_H

#include "core/common.h"
#include "core/loop.h"
#include "platform/net.h"

struct fake_n3fjp {
    struct loop *loop;
    struct plat_addr addr;
    plat_socket listener;
    plat_socket client;
    char reply[512]; /* sent as is, so it ends with "\r\n" */
    bool silent;     /* never answer */
    int accepted;
    int requests;      /* request lines received */
    char request[128]; /* the last request line, without its newline */
    size_t request_len;
};

void fake_n3fjp_start(struct fake_n3fjp *f, struct loop *loop,
                      const char *mhz);
void fake_n3fjp_stop(struct fake_n3fjp *f);

/* Makes the reply ACLog's READBMFRESPONSE, with FREQ set to mhz. */
void fake_n3fjp_set_freq(struct fake_n3fjp *f, const char *mhz);

/* Sends text to the connected client now, unasked. */
void fake_n3fjp_send(struct fake_n3fjp *f, const char *text);

/* Says goodbye as N3FJP does when it exits, and closes the connection. */
void fake_n3fjp_hang_up(struct fake_n3fjp *f);

#endif /* MOAS_TESTS_FAKE_N3FJP_H */
