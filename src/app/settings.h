/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moasweb's settings, as kept in its settings file (a JSON object) and
 * edited by the page's settings section.  The format is the Go version's:
 *
 *   { "Version": 1, "Server": "127.0.0.1:12059", "Listen": "127.0.0.1:8080",
 *     "Station": 1, "Rows": 6, "Columns": 5,
 *     "Radios": [ { "Radio": 1, "Sources": [ { "Type": "n1mm", ... } ] } ],
 *     "RecentServers": [ ... ] }
 *
 * Every entry is optional.  "_comment" entries are allowed in any object,
 * as in a .moas file, and ignored; saving does not keep them.  A misspelt
 * name is an error.
 */

#ifndef MOAS_APP_SETTINGS_H
#define MOAS_APP_SETTINGS_H

#include "cJSON.h"
#include "core/caps.h"
#include "core/common.h"
#include "core/str.h"
#include "freq/source.h"
#include "platform/net.h"

/* The Version written in settings files. */
#define APP_SETTINGS_FORMAT 1

/* Servers remembered for the settings page, most recent first. */
#define APP_RECENT_SERVERS 6

/*
 * A problem with one setting.  field names it as the page does, such as
 * "Radios.0.Sources.1.Address"; empty when it is not about one field.
 */
struct app_field_error {
    char field[96];
    char message[256];
};

struct app_radio_settings {
    int radio;
    /* Highest priority first; with none, the frequency is only typed. */
    struct freq_source_config sources[CAPS_FREQ_SOURCES];
    size_t source_count;
};

struct app_settings {
    int version;
    char server[PLAT_ADDR_TEXT_MAX];
    struct plat_addr server_addr;
    char listen[PLAT_ADDR_TEXT_MAX];
    struct plat_addr listen_addr;
    int station;
    int rows; /* the grid size to ask the server for */
    int columns;
    struct app_radio_settings radios[CAPS_OASP_RADIOS];
    size_t radio_count;
    char recent[APP_RECENT_SERVERS][PLAT_ADDR_TEXT_MAX];
    size_t recent_count;
};

/* The defaults: 127.0.0.1:12059, page at 127.0.0.1:8080, radios 1 and 2. */
void app_settings_defaults(struct app_settings *s);

/* Frees the sources' settings; the struct can then be filled again. */
void app_settings_fini(struct app_settings *s);

/*
 * Reads settings from JSON text over *s, which holds the defaults (or the
 * settings in effect).  Entries the text leaves out keep their values;
 * radios are replaced only if the text lists them (*has_radios says so).
 * *had_comments says whether there were "_comment" entries.  Everything is
 * checked: on a problem, returns ERR_INVALID with err filled in, and *s is
 * left holding the defaults.
 */
int app_settings_parse(struct str text, struct app_settings *s,
                       bool *has_radios, bool *had_comments,
                       struct app_field_error *err);

/*
 * Applies as much of the text as is usable, in order, stopping at the first
 * problem: for showing a file with a mistake in it, with the mistake marked.
 */
void app_settings_parse_partial(struct str text, struct app_settings *s);

/* The same, from JSON already parsed (the page's save). */
int app_settings_from_json(const cJSON *json, struct app_settings *s,
                           bool *has_radios, struct app_field_error *err);

/* The settings as JSON, defaults filled in, as saved and shown. */
cJSON *app_settings_to_json(const struct app_settings *s);

/* A deep copy of src into *dst (which must not hold anything). */
int app_settings_copy(struct app_settings *dst,
                      const struct app_settings *src);

/* True if a and b would run the same frequency sources. */
bool app_settings_same_sources(const struct app_settings *a,
                               const struct app_settings *b);

/* Puts server first in the recent servers. */
void app_settings_remember_server(struct app_settings *s, const char *server);

#endif /* MOAS_APP_SETTINGS_H */
