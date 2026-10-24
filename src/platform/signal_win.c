/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Stop requests on Windows: Ctrl+C, Ctrl+Break, and closing the console
 * window.  Windows calls the handler on a thread of its own; sending on a
 * socket from another thread is allowed.
 */

#include <winsock2.h>
#include <windows.h>

#include "platform/err.h"
#include "platform/signal.h"

static plat_socket wake_write = PLAT_INVALID_SOCKET;
static plat_socket wake_read = PLAT_INVALID_SOCKET;
static volatile LONG requests;

static BOOL WINAPI
on_ctrl(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        /*
         * The second request goes to the default handler, which ends the
         * program at once, in case the clean shutdown hangs.
         */
        if (InterlockedIncrement(&requests) > 1) {
            return FALSE;
        }
        send((SOCKET)wake_write, "s", 1, 0);
        if (type == CTRL_CLOSE_EVENT) {
            /*
             * Windows ends the process when this returns; give the main
             * thread a moment to shut down cleanly first.
             */
            Sleep(2000);
        }
        return TRUE;
    default:
        return FALSE;
    }
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
    requests = 0;
    if (!SetConsoleCtrlHandler(on_ctrl, TRUE)) {
        err_set_os_code((int)GetLastError());
        plat_signal_close();
        return ERR_IO;
    }
    *notify = wake_read;
    return 0;
}

void
plat_signal_close(void)
{
    SetConsoleCtrlHandler(on_ctrl, FALSE);
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
