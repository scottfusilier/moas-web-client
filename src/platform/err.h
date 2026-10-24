/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Error codes.  A function that can fail returns 0 on success or one of
 * these.  They live in the lowest layer so that every layer can use them.
 */

#ifndef MOAS_PLATFORM_ERR_H
#define MOAS_PLATFORM_ERR_H

enum {
    ERR_NOMEM = 1,   /* out of memory */
    ERR_LIMIT,       /* larger than an allowed size */
    ERR_INVALID,     /* malformed input or an unusable argument */
    ERR_RANGE,       /* a number outside the allowed range */
    ERR_AGAIN,       /* would block; try again when the socket is ready */
    ERR_CLOSED,      /* the other end closed or reset the connection */
    ERR_REFUSED,     /* the connection was refused */
    ERR_TIMEOUT,     /* the other end did not answer in time */
    ERR_ADDR,        /* not a usable network address */
    ERR_ADDR_IN_USE, /* the address is already in use */
    ERR_NOT_FOUND,   /* no such file or folder */
    ERR_ACCESS,      /* permission denied */
    ERR_IO,          /* any other failure reported by the system */
};

/* A short description of an error code, such as "connection refused". */
const char *err_str(int err);

/*
 * The system's own error number (errno, or GetLastError() and
 * WSAGetLastError() on Windows) behind the most recent error returned by the
 * platform layer, for diagnostics; 0 if there is none.
 */
int err_os_code(void);

/* Records the system error number; used by the platform layer only. */
void err_set_os_code(int code);

#endif /* MOAS_PLATFORM_ERR_H */
