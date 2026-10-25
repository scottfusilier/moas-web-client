/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A stand-in for hamlib's rigctld, for tests: listens on a free loopback
 * port, on the same event loop as the code being tested, and answers each
 * command line with reply (change it at any time).
 */

#ifndef MOAS_TESTS_FAKE_RIG_H
#define MOAS_TESTS_FAKE_RIG_H

#include "core/common.h"
#include "core/loop.h"
#include "platform/net.h"

struct fake_rig {
    struct loop *loop;
    struct plat_addr addr;
    plat_socket listener;
    plat_socket client;
    char reply[64];
    bool silent; /* never answer */
    int accepted;
};

void fake_rig_start(struct fake_rig *rig, struct loop *loop,
                    const char *reply);
void fake_rig_stop(struct fake_rig *rig);

#endif /* MOAS_TESTS_FAKE_RIG_H */
