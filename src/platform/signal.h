/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Requests to stop: Ctrl+C, and SIGTERM on Unix or closing the console
 * window on Windows.  Each one makes a socket readable, so the event loop
 * can watch for it and the program can shut down cleanly.  The signal
 * handler itself only sends one byte.
 *
 * A second Ctrl+C stops the program at once, in case the clean shutdown
 * hangs.
 */

#ifndef MOAS_PLATFORM_SIGNAL_H
#define MOAS_PLATFORM_SIGNAL_H

#include "platform/net.h"

/*
 * Starts catching stop requests.  *notify becomes readable when one
 * arrives; watch it for reading.  Call once per process, after
 * plat_net_init().
 */
int plat_signal_open(plat_socket *notify);

/* Stops catching them, restoring the system's default behaviour. */
void plat_signal_close(void);

/* Reads and discards what is waiting on notify. */
void plat_signal_drain(plat_socket notify);

/* Makes a stop request from inside the program, as Ctrl+C would. */
void plat_signal_raise(void);

#endif /* MOAS_PLATFORM_SIGNAL_H */
