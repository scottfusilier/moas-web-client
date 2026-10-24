/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "platform/err.h"

/* The program has one thread, so one variable is enough. */
static int last_os_code;

const char *
err_str(int err)
{
    switch (err) {
    case 0:
        return "success";
    case ERR_NOMEM:
        return "out of memory";
    case ERR_LIMIT:
        return "too large";
    case ERR_INVALID:
        return "invalid";
    case ERR_RANGE:
        return "out of range";
    case ERR_AGAIN:
        return "would block";
    case ERR_CLOSED:
        return "connection closed";
    case ERR_REFUSED:
        return "connection refused";
    case ERR_TIMEOUT:
        return "timed out";
    case ERR_ADDR:
        return "not a usable address";
    case ERR_ADDR_IN_USE:
        return "address already in use";
    case ERR_NOT_FOUND:
        return "not found";
    case ERR_ACCESS:
        return "permission denied";
    case ERR_IO:
        return "system error";
    default:
        return "unknown error";
    }
}

int
err_os_code(void)
{
    return last_os_code;
}

void
err_set_os_code(int code)
{
    last_os_code = code;
}
