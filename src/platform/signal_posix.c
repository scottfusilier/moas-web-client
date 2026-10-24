/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Stop requests on FreeBSD, Linux and macOS: SIGINT and SIGTERM. */

#include <errno.h>
#include <signal.h>
#include <string.h>

#include <sys/socket.h>

#include "platform/err.h"
#include "platform/signal.h"

/* Set before the handlers are installed, and only read by them after. */
static plat_socket wake_write = PLAT_INVALID_SOCKET;
static plat_socket wake_read = PLAT_INVALID_SOCKET;

/*
 * Runs in signal context, so it may only use async-signal-safe functions:
 * send() and signal() are.  errno is saved because the code interrupted may be
 * looking at it.
 */
static void
on_signal(int sig)
{
    int saved = errno;
    send(wake_write, "s", 1, 0);
    /*
     * Back to the default action, so that a second Ctrl+C stops the program
     * even if the clean shutdown hangs.
     */
    signal(sig, SIG_DFL);
    errno = saved;
}

static int
install(void (*handler)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0)
    {
        err_set_os_code(errno);
        return ERR_IO;
    }
    return 0;
}

int
plat_signal_open(plat_socket *notify)
{
    if (wake_write != PLAT_INVALID_SOCKET) {
        return ERR_INVALID;
    }
    int err = plat_socket_pair(&wake_read, &wake_write);
    if (err != 0) {
        return err;
    }
    err = install(on_signal);
    if (err != 0) {
        plat_signal_close();
        return err;
    }
    *notify = wake_read;
    return 0;
}

void
plat_signal_close(void)
{
    install(SIG_DFL);
    plat_socket_close(wake_write);
    plat_socket_close(wake_read);
    wake_write = PLAT_INVALID_SOCKET;
    wake_read = PLAT_INVALID_SOCKET;
}

void
plat_signal_drain(plat_socket notify)
{
    char buf[64];
    size_t got;

    while (plat_recv(notify, buf, sizeof(buf), &got) == 0 && got > 0) {
    }
}

void
plat_signal_raise(void)
{
    size_t sent;

    if (wake_write != PLAT_INVALID_SOCKET) {
        plat_send(wake_write, "s", 1, &sent);
    }
}
