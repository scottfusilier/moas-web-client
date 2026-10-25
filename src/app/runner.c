/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/runner.h"
#include "app/settings_file.h"
#include "core/json.h"
#include "core/log.h"
#include "freq/n1mm.h"
#include "freq/source.h"

#define DEFAULT_RETRY_MS 2000

struct app_runner;

/*
 * One radio's sources and the frequency followed from them.  A frequency is
 * sent when the source being followed changes or reports a new frequency;
 * a frequency typed on the page therefore holds until the radio is tuned or
 * the followed source changes: the last change wins.
 */
struct followed {
    struct app_runner *runner;
    struct oasp_radio_id id;
    struct freq_tracker *tracker;
    struct freq_source *sources[CAPS_FREQ_SOURCES];
    size_t source_count;
    int index; /* the source last followed; -1 for none */
    int khz;   /* its frequency then */
};

struct app_runner {
    struct loop *loop;
    char path[CAPS_PATH];
    char listen[PLAT_ADDR_TEXT_MAX];
    char override_text[APP_MAX_OVERRIDES][16];
    const char *overrides[APP_MAX_OVERRIDES];
    size_t override_count;
    app_changed_fn *changed;
    void *arg;

    struct app_settings settings;
    struct app_link *link;
    struct freq_env *env;
    struct followed radios[CAPS_OASP_RADIOS];
    size_t radio_count;
};

static void
on_link_changed(void *arg)
{
    struct app_runner *r = arg;
    if (r->changed != NULL) {
        r->changed(r->arg);
    }
}

/* A tracker's view changed: follow it, and show the new source states. */
static void
on_tracker_changed(struct freq_tracker *tracker, void *arg)
{
    struct followed *f = arg;
    int khz = 0;
    const char *name = NULL;
    int index = freq_tracker_choose(tracker, &khz, &name);

    if (index != f->index || khz != f->khz) {
        f->index = index;
        f->khz = khz;
        if (index >= 0) {
            /* Not connected only means it is sent on connecting. */
            app_link_set_frequency(f->runner->link, f->id, khz, name);
        }
    }
    app_link_notify(f->runner->link);
}

static void
stop_sources(struct app_runner *r)
{
    for (size_t i = 0; i < r->radio_count; i++) {
        struct followed *f = &r->radios[i];
        for (size_t j = 0; j < f->source_count; j++) {
            freq_source_stop(f->sources[j]);
        }
        freq_tracker_destroy(f->tracker);
        memset(f, 0, sizeof(*f));
    }
    r->radio_count = 0;
    app_link_untrack_all(r->link);
}

static void
start_sources(struct app_runner *r)
{
    const struct app_settings *s = &r->settings;
    for (size_t i = 0; i < s->radio_count; i++) {
        const struct app_radio_settings *rs = &s->radios[i];
        if (rs->source_count == 0) {
            continue;
        }
        struct followed *f = &r->radios[r->radio_count];
        memset(f, 0, sizeof(*f));
        f->runner = r;
        f->id.station = s->station;
        f->id.radio = rs->radio;
        f->index = -1;
        if (freq_tracker_create(r->loop, on_tracker_changed, f, &f->tracker) !=
            0)
        {
            log_error("radio %d: out of memory for its sources", rs->radio);
            continue;
        }
        r->radio_count++;
        for (size_t j = 0; j < rs->source_count; j++) {
            const struct freq_source_config *c = &rs->sources[j];
            struct freq_feed feed;
            int err = freq_tracker_add(f->tracker, c->name, c->timeout_ms,
                                       &feed);
            if (err == 0) {
                err = freq_source_start(r->env, c, feed,
                                        &f->sources[f->source_count]);
            }
            if (err != 0) {
                log_error("radio %d: cannot start %s: %s", rs->radio, c->name,
                          err_str(err));
                continue;
            }
            f->source_count++;
        }
        app_link_track(r->link, f->id, f->tracker);
    }
}

int
app_runner_create(struct loop *loop, const struct app_runner_options *o,
                  const struct app_settings *s, app_changed_fn *changed,
                  void *arg, struct app_runner **out)
{
    struct app_runner *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return ERR_NOMEM;
    }
    r->loop = loop;
    r->changed = changed;
    r->arg = arg;
    snprintf(r->path, sizeof(r->path), "%s", o->path);
    snprintf(r->listen, sizeof(r->listen), "%s", o->listen);
    for (size_t i = 0; i < o->override_count && i < APP_MAX_OVERRIDES; i++) {
        snprintf(r->override_text[i], sizeof(r->override_text[i]), "%s",
                 o->overrides[i]);
        r->overrides[i] = r->override_text[i];
        r->override_count++;
    }

    int err = app_settings_copy(&r->settings, s);
    if (err == 0) {
        err = freq_env_create(loop, &r->env);
    }
    if (err == 0) {
        err = app_link_create(loop, &r->settings,
                              o->retry_ms != 0 ? o->retry_ms
                                               : DEFAULT_RETRY_MS,
                              on_link_changed, r, &r->link);
    }
    if (err != 0) {
        freq_env_destroy(r->env);
        app_settings_fini(&r->settings);
        free(r);
        return err;
    }
    start_sources(r);
    *out = r;
    return 0;
}

void
app_runner_destroy(struct app_runner *r)
{
    if (r == NULL) {
        return;
    }
    /* Going away is not a change to report: the page may be gone already. */
    r->changed = NULL;
    stop_sources(r);
    app_link_destroy(r->link);
    freq_env_destroy(r->env);
    app_settings_fini(&r->settings);
    free(r);
}

struct app_link *
app_runner_link(struct app_runner *r)
{
    return r->link;
}

const struct app_settings *
app_runner_settings(const struct app_runner *r)
{
    return &r->settings;
}

const char *
app_runner_path(const struct app_runner *r)
{
    return r->path;
}

const char *
app_runner_listen(const struct app_runner *r)
{
    return r->listen;
}

size_t
app_runner_overrides(const struct app_runner *r, const char *const **out)
{
    *out = r->overrides;
    return r->override_count;
}

int
app_runner_save(struct app_runner *r, const struct app_settings *next,
                const char *version, bool *conflict, bool *restart,
                struct app_field_error *err)
{
    *conflict = false;
    *restart = false;
    memset(err, 0, sizeof(*err));

    char current[SHA256_HEX_SIZE];
    int e = app_settings_version(r->path, current);
    if (e != 0) {
        int n = snprintf(err->message, sizeof(err->message),
                         "cannot read %s: %s", r->path, err_str(e));
        if (n < 0 || (size_t)n >= sizeof(err->message)) {
            /* A path too long to show is left out, not cut off. */
            snprintf(err->message, sizeof(err->message),
                     "cannot read the settings file: %s", err_str(e));
        }
        return e;
    }
    if (strcmp(current, version != NULL ? version : "") != 0) {
        *conflict = true;
        snprintf(err->message, sizeof(err->message),
                 "the settings file was changed by something else since "
                 "this form was opened");
        return ERR_INVALID;
    }

    struct app_settings work;
    e = app_settings_copy(&work, next);
    if (e != 0) {
        snprintf(err->message, sizeof(err->message), "out of memory");
        return e;
    }
    /* The servers offered on the page: the old one, then the new, first. */
    app_settings_remember_server(&work, r->settings.server);
    app_settings_remember_server(&work, work.server);

    e = app_settings_save(r->path, &work, err);
    if (e != 0) {
        app_settings_fini(&work);
        return e;
    }

    bool sources_changed = !app_settings_same_sources(&r->settings, &work);
    if (sources_changed) {
        stop_sources(r);
    }
    app_settings_fini(&r->settings);
    r->settings = work;
    app_link_reconfigure(r->link, &r->settings);
    if (sources_changed) {
        start_sources(r);
    }
    *restart = strcmp(r->settings.listen, r->listen) != 0;
    return 0;
}

cJSON *
app_runner_heard(const struct app_runner *r)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    const struct app_settings *s = &r->settings;
    for (size_t i = 0; i < s->radio_count; i++) {
        for (size_t j = 0; j < s->radios[i].source_count; j++) {
            const struct freq_source_config *c = &s->radios[i].sources[j];
            if (c->ops != &freq_n1mm_ops) {
                continue;
            }
            /* The address as written in the settings, which the page uses. */
            cJSON *json = freq_source_to_json(c);
            const cJSON *address = cJSON_GetObjectItemCaseSensitive(json,
                                                                    "Address");
            if (cJSON_IsString(address) &&
                cJSON_GetObjectItemCaseSensitive(root, address->valuestring) ==
                    NULL)
            {
                struct plat_addr addr;
                const char *text = address->valuestring;
                cJSON *list = cJSON_AddArrayToObject(root, text);
                struct freq_n1mm_heard heard[CAPS_N1MM_HEARD];
                size_t n = 0;
                if (list != NULL &&
                    plat_addr_parse(text, strlen(text), &addr) == 0)
                {
                    n = freq_n1mm_heard(r->env, &addr, heard, CAPS_N1MM_HEARD);
                }
                for (size_t k = 0; list != NULL && k < n; k++) {
                    cJSON *h = cJSON_CreateObject();
                    if (h == NULL || !cJSON_AddItemToArray(list, h)) {
                        break;
                    }
                    cJSON_AddStringToObject(h, "stationName",
                                            heard[k].station_name);
                    cJSON_AddNumberToObject(h, "radioNr", heard[k].radio_nr);
                    cJSON_AddNumberToObject(h, "kHz", heard[k].khz);
                    cJSON_AddBoolToObject(h, "connected", heard[k].connected);
                }
            }
            cJSON_Delete(json);
        }
    }
    return root;
}
