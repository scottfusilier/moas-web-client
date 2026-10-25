/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moas-web-client: a client for the MOAS II antenna switch server, with its
 * buttons in a web page.  It connects to a MOAS server for one or more radios
 * and serves a page that shows the server's button grid and sends clicks back.
 *
 * Each radio's frequency can come from N1MM Logger+ or hamlib's rigctld, as
 * set in the settings file, or be typed on the page.
 *
 * By default the page is served to this computer only.  The MOAS protocol
 * has no login, so anyone who can open the page can switch antennas.
 */

#include "core/common.h"

#include <stdio.h>

#include "app/args.h"
#include "app/serve.h"
#include "app/settings_file.h"
#include "app/watch.h"
#include "core/caps.h"
#include "platform/proc.h"

int
main(int argc, char **argv)
{
    if (plat_process_init(&argc, &argv) != 0) {
        fprintf(stderr, "moas-web-client: cannot read the command line\n");
        return 1;
    }

    struct app_args args;
    char error[256];
    if (app_args_parse(argc, argv, &args, error, sizeof(error)) != 0) {
        fprintf(stderr,
                "moas-web-client: %s\nRun moas-web-client -help for the "
                "options.\n",
                error);
        return 2;
    }
    if (args.help) {
        fputs(app_args_usage(), stdout);
        return 0;
    }
    if (args.version) {
        printf("moas-web-client %s\n", MOAS_VERSION);
        return 0;
    }
    if (!args.watch) {
        return app_serve(&args, argv[0]);
    }

    /* Watch mode: the server and station from the settings file, unless
     * given. */
    char path[CAPS_PATH];
    struct app_settings s;
    char message[320];
    app_settings_defaults(&s);
    if (app_settings_find(args.config, argv[0], path, sizeof(path)) == 0 &&
        app_startup_settings(path, &args, true, &s, message,
                             sizeof(message)) == 0)
    {
        static char server[PLAT_ADDR_TEXT_MAX];
        snprintf(server, sizeof(server), "%s", s.server);
        args.server = server;
        args.station = s.station;
    }
    app_settings_fini(&s);
    return app_watch_run(&args);
}
