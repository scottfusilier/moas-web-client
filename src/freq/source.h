/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Frequency sources: programs that report a radio's frequency, such as
 * N1MM Logger+, DXLog.net or hamlib's rigctld.
 *
 * Each kind of source is a "type" with a table of operations
 * (struct freq_source_ops), listed in registry.c.  Adding a type means
 * writing one file that defines its table, and adding one line to
 * registry.c; nothing else changes.  docs/adding-a-frequency-source.md walks
 * through it.
 *
 * A source never knows about radios' priorities or the MOAS server.  It
 * reports readings and failures through the feed it is given, and the
 * tracker (freq/tracker.h) decides what to believe.
 */

#ifndef MOAS_FREQ_SOURCE_H
#define MOAS_FREQ_SOURCE_H

#include "cJSON.h"
#include "core/caps.h"
#include "core/common.h"
#include "core/loop.h"
#include "freq/tracker.h"
#include "platform/net.h"

/* A problem with one setting: which field, and what is wrong with it. */
struct freq_field_error {
    char field[64];    /* such as "PollMilliseconds" */
    char message[256]; /* such as "PollMilliseconds must be at least 50" */
};

/* Fills in a field error; returns ERR_INVALID for convenience. */
int freq_field_error(struct freq_field_error *err, const char *field,
                     const char *fmt, ...) MOAS_PRINTF(3, 4);

struct freq_source_ops;

/* One source's settings, checked and ready to start. */
struct freq_source_config {
    const struct freq_source_ops *ops;
    char name[CAPS_FREQ_NAME]; /* shown on the page */
    uint32_t timeout_ms; /* readings older than this are stale; 0 never */
    void *data;          /* the type's own settings */
};

/*
 * Shared state for all sources, such as the N1MM listeners that radios on
 * the same address share.  One per program.
 */
struct freq_env;

/* A running source.  Each type's struct starts with one of these. */
struct freq_source {
    const struct freq_source_ops *ops;
};

struct freq_source_ops {
    /* The name used in the settings file: "n1mm", "rigctld". */
    const char *type;

    /*
     * Reads and checks the type's settings from json (one entry of a
     * radio's "Sources"), filling in defaults.  moas_radio is the MOAS
     * radio number the source is for.  Fills config->name, ->timeout_ms
     * and ->data.  On a problem, returns ERR_INVALID with err filled in.
     */
    int (*parse)(const cJSON *json, int moas_radio,
                 struct freq_source_config *config,
                 struct freq_field_error *err);

    /* Frees config->data. */
    void (*free_data)(void *data);

    /*
     * Writes the settings back into obj (which already holds "Type"), with
     * defaults filled in, as they are saved to the settings file and shown
     * on the page.  Settings left at their "unset" value may be left out.
     */
    int (*write)(const struct freq_source_config *config, cJSON *obj);

    /* Starts the source; it reports through feed until stopped. */
    int (*start)(struct freq_env *env, const struct freq_source_config *config,
                 struct freq_feed feed, struct freq_source **out);

    void (*stop)(struct freq_source *source);

    /* Frees the type's shared state in env (may be NULL if it has none). */
    void (*env_fini)(void *shared);
};

/* Registry (registry.c). */

/* The type called name (ignoring case and surrounding spaces), or NULL. */
const struct freq_source_ops *freq_find_type(const char *name);

/* "n1mm", "rigctld", ...: the known types, for messages. */
const char *freq_type_list(void);

/*
 * Reads one source's settings: finds its "Type" and lets the type check
 * the rest.  Field names in err are relative to the source.
 */
int freq_parse_source(const cJSON *json, int moas_radio,
                      struct freq_source_config *config,
                      struct freq_field_error *err);

void freq_source_config_fini(struct freq_source_config *config);

/*
 * The source's settings as a JSON object ({"Type": ..., ...}), for the
 * settings file and the page.  NULL if out of memory.
 */
cJSON *freq_source_to_json(const struct freq_source_config *config);

int freq_env_create(struct loop *loop, struct freq_env **out);
void freq_env_destroy(struct freq_env *env);
struct loop *freq_env_loop(const struct freq_env *env);

/* Where a type keeps its shared state; NULL until the type sets it. */
void **freq_env_shared(struct freq_env *env,
                       const struct freq_source_ops *ops);

int freq_source_start(struct freq_env *env,
                      const struct freq_source_config *config,
                      struct freq_feed feed, struct freq_source **out);
void freq_source_stop(struct freq_source *source);

/*
 * Helpers for parse functions.  freq_check_keys() refuses any member of
 * json not in allowed[] (a NULL-terminated list), except "Type" and those
 * whose names start with "_comment".
 */
int freq_check_keys(const cJSON *json, const char *const *allowed,
                    struct freq_field_error *err);

/*
 * Reads json's "Address" (host:port with a numeric host), or uses
 * default_address if there is none.  *text gets it as given or defaulted.
 */
int freq_parse_address(const cJSON *json, const char *default_address,
                       struct plat_addr *addr, char *text, size_t text_size,
                       struct freq_field_error *err);

/*
 * Reads json's "TimeoutSeconds" (0 to 3600, fractions allowed), or uses
 * default_ms if there is none or it is 0.
 */
int freq_parse_timeout(const cJSON *json, uint32_t default_ms,
                       uint32_t *timeout_ms, struct freq_field_error *err);

#endif /* MOAS_FREQ_SOURCE_H */
