/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "app/runner.h"
#include "app/serve.h"
#include "app/settings_file.h"
#include "app/web.h"
#include "core/caps.h"
#include "core/log.h"
#include "platform/signal.h"

/* "1,2" into [{"Radio":1},{"Radio":2}]. */
static cJSON *
radio_list(const char *list, char *message, size_t message_size)
{
    cJSON *radios = cJSON_CreateArray();
    struct str rest = str_from_cstr(list);
    while (radios != NULL) {
        struct str item;
        struct str after;
        bool more = str_split(rest, ',', &item, &after);
        if (!more) {
            item = rest;
        }
        int64_t number = 0;
        item = str_trim(item);
        if (str_to_i64(item, 1, 255, &number) != 0) {
            snprintf(message, message_size,
                     "-radios: \"%.*s\" is not a radio number", (int)item.len,
                     item.ptr);
            cJSON_Delete(radios);
            return NULL;
        }
        cJSON *radio = cJSON_CreateObject();
        if (radio == NULL || !cJSON_AddItemToArray(radios, radio) ||
            cJSON_AddNumberToObject(radio, "Radio", (double)number) == NULL)
        {
            break;
        }
        if (!more) {
            return radios;
        }
        rest = after;
    }
    cJSON_Delete(radios);
    snprintf(message, message_size, "out of memory");
    return NULL;
}

int
app_startup_settings(const char *path, const struct app_args *args, bool quiet,
                     struct app_settings *s, char *message,
                     size_t message_size)
{
    struct app_settings_file info;
    struct app_field_error err;

    app_settings_defaults(s);
    int e = app_settings_load(path, s, &info, &err);
    if (e == ERR_NOT_FOUND) {
        if (!quiet) {
            printf("No settings file at %s yet; using defaults.  Saving "
                   "settings on the page creates it.\n",
                   path);
        }
    } else if (e != 0) {
        if (!quiet) {
            printf("The settings file cannot be used: %s: %s%s%s\n", path,
                   err.field, err.field[0] != '\0' ? ": " : "", err.message);
            printf("Using defaults until it is fixed, by hand or on the "
                   "page's settings section.  The file has not been "
                   "changed.\n");
        }
        info.has_radios = false;
    }

    /* Options given on the command line win over the file. */
    cJSON *over = cJSON_CreateObject();
    if (over == NULL) {
        snprintf(message, message_size, "out of memory");
        return ERR_NOMEM;
    }
    if (args->given[APP_OPT_SERVER]) {
        cJSON_AddStringToObject(over, "Server", args->server);
    }
    if (args->given[APP_OPT_LISTEN]) {
        cJSON_AddStringToObject(over, "Listen", args->listen);
    }
    if (args->given[APP_OPT_STATION]) {
        cJSON_AddNumberToObject(over, "Station", args->station);
    }
    if (args->given[APP_OPT_ROWS]) {
        cJSON_AddNumberToObject(over, "Rows", args->rows);
    }
    if (args->given[APP_OPT_COLUMNS]) {
        cJSON_AddNumberToObject(over, "Columns", args->columns);
    }
    if (args->given[APP_OPT_RADIOS]) {
        if (info.has_radios) {
            snprintf(message, message_size,
                     "-radios cannot be used with a settings file that lists "
                     "radios");
            cJSON_Delete(over);
            return ERR_INVALID;
        }
        cJSON *radios = radio_list(args->radios, message, message_size);
        if (radios == NULL) {
            cJSON_Delete(over);
            return ERR_INVALID;
        }
        cJSON_AddItemToObject(over, "Radios", radios);
    }
    e = app_settings_from_json(over, s, NULL, &err);
    cJSON_Delete(over);
    if (e != 0) {
        /* The file was usable on its own, so the command line is at fault. */
        snprintf(message, message_size, "command line: %s", err.message);
        return ERR_INVALID;
    }
    return 0;
}

/* The page, created after the runner, receives the runner's changes. */
struct serving {
    struct app_web *web;
};

static void
on_changed(void *arg)
{
    struct serving *s = arg;
    app_web_changed(s->web);
}

static void
on_stop_request(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)events;
    (void)arg;
    plat_signal_drain(sock);
    loop_stop(loop);
}

int
app_serve(const struct app_args *args, const char *argv0)
{
    char path[CAPS_PATH];
    if (app_settings_find(args->config, argv0, path, sizeof(path)) != 0) {
        fprintf(stderr, "moasweb: the settings file's path is too long\n");
        return 2;
    }
    struct app_settings settings;
    char message[320];
    if (app_startup_settings(path, args, false, &settings, message,
                             sizeof(message)) != 0)
    {
        fprintf(stderr, "moasweb: %s\n", message);
        app_settings_fini(&settings);
        return 2;
    }

    struct app_runner_options options = {
        .path = path,
        .listen = settings.listen,
    };
    for (int i = 0; i < APP_OPT_COUNT; i++) {
        if (args->given[i] && i != APP_OPT_CONFIG &&
            options.override_count < APP_MAX_OVERRIDES)
        {
            static char names[APP_OPT_COUNT][16];
            snprintf(names[i], sizeof(names[i]), "-%s",
                     app_args_name((enum app_option)i));
            options.overrides[options.override_count++] = names[i];
        }
    }

    int status = 1;
    struct loop *loop = NULL;
    struct app_runner *runner = NULL;
    struct serving serving = {0};
    plat_socket stop = PLAT_INVALID_SOCKET;
    bool signals = false;

    int err = plat_net_init();
    if (err != 0) {
        log_error("cannot start networking: %s", err_str(err));
        app_settings_fini(&settings);
        return 1;
    }
    err = loop_create(&loop);
    if (err == 0) {
        err = plat_signal_open(&stop);
        signals = err == 0;
    }
    if (err == 0) {
        err = loop_watch(loop, stop, LOOP_READ, on_stop_request, NULL);
    }
    if (err == 0) {
        err = app_runner_create(loop, &options, &settings, on_changed,
                                &serving, &runner);
    }
    if (err != 0) {
        log_error("cannot start: %s", err_str(err));
        goto out;
    }

    const struct http_server_options web_options = {
        .listen = settings.listen_addr,
    };
    err = app_web_create(loop, runner, &web_options, &serving.web);
    if (err != 0) {
        fprintf(stderr, "moasweb: cannot serve the page at %s: %s\n",
                settings.listen, err_str(err));
        goto out;
    }

    printf("Settings file: %s\n", path);
    printf("MOAS server: %s\n", settings.server);
    for (size_t i = 0; i < settings.radio_count; i++) {
        const struct app_radio_settings *r = &settings.radios[i];
        for (size_t j = 0; j < r->source_count; j++) {
            printf("Radio %d follows %s\n", r->radio, r->sources[j].name);
        }
    }
    char where[PLAT_ADDR_TEXT_MAX];
    plat_addr_format(app_web_addr(serving.web), where);
    printf("Open http://%s/ in a browser.  Press Ctrl+C to stop.\n", where);
    if (!plat_addr_is_loopback(&settings.listen_addr)) {
        printf("Warning: the page is reachable from other computers, and "
               "anyone who opens it can switch antennas.\n");
    }
    fflush(stdout);

    err = loop_run(loop);
    if (err != 0) {
        log_error("stopped: %s", err_str(err));
    } else {
        status = 0;
    }

out:
    app_web_destroy(serving.web);
    serving.web = NULL;
    app_runner_destroy(runner);
    if (signals) {
        loop_unwatch(loop, stop);
        plat_signal_close();
    }
    if (loop != NULL) {
        loop_destroy(loop);
    }
    plat_net_fini();
    app_settings_fini(&settings);
    return status;
}
