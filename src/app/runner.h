/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The settings in effect, and what they run: the link to the MOAS server,
 * and each radio's frequency sources with the tracker that follows them.
 * Saving settings from the page applies them without a restart, except for
 * the page's own address.
 */

#ifndef MOAS_APP_RUNNER_H
#define MOAS_APP_RUNNER_H

#include "app/link.h"
#include "app/settings.h"
#include "cJSON.h"
#include "core/caps.h"
#include "core/loop.h"

/* Command-line options that overrode the file, shown on the page. */
#define APP_MAX_OVERRIDES 8

struct app_runner_options {
    const char *path;   /* the settings file */
    const char *listen; /* the address the page is actually served on */
    const char *overrides[APP_MAX_OVERRIDES]; /* such as "-server" */
    size_t override_count;
    uint32_t retry_ms; /* the link's first retry pause; 0: the default */
};

struct app_runner;

/*
 * Starts the link and the sources for s (copied).  changed is called
 * whenever the snapshot would be different.
 */
int app_runner_create(struct loop *loop, const struct app_runner_options *o,
                      const struct app_settings *s, app_changed_fn *changed,
                      void *arg, struct app_runner **out);
void app_runner_destroy(struct app_runner *runner);

struct app_link *app_runner_link(struct app_runner *runner);
const struct app_settings *app_runner_settings(const struct app_runner *r);
const char *app_runner_path(const struct app_runner *runner);
const char *app_runner_listen(const struct app_runner *runner);
size_t app_runner_overrides(const struct app_runner *runner,
                            const char *const **out);

/*
 * Saves checked settings to the file and applies them.  version is the
 * file's version when the page read it: if the file has changed since,
 * nothing is saved and *conflict is set.  *restart says whether the page's
 * address changed, which takes a restart.  On failure err says why.
 */
int app_runner_save(struct app_runner *runner, const struct app_settings *next,
                    const char *version, bool *conflict, bool *restart,
                    struct app_field_error *err);

/*
 * For each address the N1MM sources listen on, the radios heard there:
 * { "127.0.0.1:12060": [ { "stationName", "radioNr", "kHz",
 * "connected" }, ... ] }.
 */
cJSON *app_runner_heard(const struct app_runner *runner);

#endif /* MOAS_APP_RUNNER_H */
