/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Running moas-web-client for the command line: the page, or watch mode. */

#ifndef MOAS_APP_SERVE_H
#define MOAS_APP_SERVE_H

#include "app/args.h"
#include "app/settings.h"

/*
 * The settings to start with: the file's (from path), with the command-line
 * options given in args over them.  A missing or unusable file is not
 * fatal: the defaults are used and the problem is printed (the page shows
 * it too).  Returns 0, or ERR_INVALID with message filled in when the
 * command line itself is at fault.
 */
int app_startup_settings(const char *path, const struct app_args *args,
                         bool quiet, struct app_settings *s, char *message,
                         size_t message_size);

/* Serves the page until Ctrl+C.  Returns the program's exit status. */
int app_serve(const struct app_args *args, const char *argv0);

#endif /* MOAS_APP_SERVE_H */
