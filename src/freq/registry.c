/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The known source types, and what every type shares.  To add a type,
 * write its file and add its table to "types" below.
 */

#include "core/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/str.h"
#include "freq/n1mm.h"
#include "freq/n3fjp.h"
#include "freq/rigctld.h"
#include "freq/source.h"

static const struct freq_source_ops *const types[] = {
    &freq_n1mm_ops,
    &freq_dxlog_ops,
    &freq_rigctld_ops,
    &freq_n3fjp_ops,
};

struct freq_env {
    struct loop *loop;
    void *shared[ARRAY_LEN(types)];
};

int
freq_field_error(struct freq_field_error *err, const char *field,
                 const char *fmt, ...)
{
    va_list ap;

    snprintf(err->field, sizeof(err->field), "%s", field);
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
    return ERR_INVALID;
}

const struct freq_source_ops *
freq_find_type(const char *name)
{
    struct str n = str_trim(str_from_cstr(name));
    for (size_t i = 0; i < ARRAY_LEN(types); i++) {
        if (str_eq_nocase(n, str_from_cstr(types[i]->type))) {
            return types[i];
        }
    }
    return NULL;
}

const char *
freq_type_list(void)
{
    return "\"n1mm\", \"dxlog\", \"rigctld\" or \"n3fjp\"";
}

int
freq_parse_source(const cJSON *json, int moas_radio,
                  struct freq_source_config *config,
                  struct freq_field_error *err)
{
    memset(config, 0, sizeof(*config));
    if (!cJSON_IsObject(json)) {
        return freq_field_error(err, "", "a source must be an object");
    }
    char type[32] = "";
    int e = json_get_text(json, "Type", type, sizeof(type));
    if (e == ERR_LIMIT) {
        return freq_field_error(err, "Type",
                                "Type is not a known source; "
                                "use %s",
                                freq_type_list());
    }
    if (e != 0) {
        return freq_field_error(err, "Type", "Type must be text");
    }
    const struct freq_source_ops *ops = freq_find_type(type);
    if (ops == NULL) {
        return freq_field_error(err, "Type",
                                "Type \"%s\" is not a known source; use %s",
                                type, freq_type_list());
    }
    config->ops = ops;
    e = ops->parse(json, moas_radio, config, err);
    if (e != 0) {
        freq_source_config_fini(config);
    }
    return e;
}

void
freq_source_config_fini(struct freq_source_config *config)
{
    if (config->ops != NULL && config->data != NULL) {
        config->ops->free_data(config->data);
    }
    memset(config, 0, sizeof(*config));
}

cJSON *
freq_source_to_json(const struct freq_source_config *config)
{
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
        return NULL;
    }
    if (cJSON_AddStringToObject(obj, "Type", config->ops->type) == NULL ||
        config->ops->write(config, obj) != 0)
    {
        cJSON_Delete(obj);
        return NULL;
    }
    return obj;
}

int
freq_env_create(struct loop *loop, struct freq_env **out)
{
    struct freq_env *env = calloc(1, sizeof(*env));
    if (env == NULL) {
        return ERR_NOMEM;
    }
    env->loop = loop;
    *out = env;
    return 0;
}

void
freq_env_destroy(struct freq_env *env)
{
    if (env == NULL) {
        return;
    }
    for (size_t i = 0; i < ARRAY_LEN(types); i++) {
        if (env->shared[i] != NULL && types[i]->env_fini != NULL) {
            types[i]->env_fini(env->shared[i]);
        }
    }
    free(env);
}

struct loop *
freq_env_loop(const struct freq_env *env)
{
    return env->loop;
}

void **
freq_env_shared(struct freq_env *env, const struct freq_source_ops *ops)
{
    for (size_t i = 0; i < ARRAY_LEN(types); i++) {
        if (types[i] == ops) {
            return &env->shared[i];
        }
    }
    return NULL;
}

int
freq_source_start(struct freq_env *env,
                  const struct freq_source_config *config,
                  struct freq_feed feed, struct freq_source **out)
{
    return config->ops->start(env, config, feed, out);
}

void
freq_source_stop(struct freq_source *source)
{
    if (source != NULL) {
        source->ops->stop(source);
    }
}

int
freq_parse_address(const cJSON *json, const char *default_address,
                   struct plat_addr *addr, char *text, size_t text_size,
                   struct freq_field_error *err)
{
    char given[PLAT_ADDR_TEXT_MAX] = "";
    if (json_get_text(json, "Address", given, sizeof(given)) != 0) {
        return freq_field_error(err, "Address",
                                "Address must be text such as \"%s\"",
                                default_address);
    }
    struct str a = str_trim(str_from_cstr(given));
    if (a.len == 0) {
        a = str_from_cstr(default_address);
    }
    if (plat_addr_parse(a.ptr, a.len, addr) != 0) {
        return freq_field_error(err, "Address",
                                "Address \"%.*s\" is not host:port with a "
                                "numeric host, such as \"%s\"",
                                (int)a.len, a.ptr, default_address);
    }
    if (str_copy_clean(a, text, text_size) != 0) {
        return freq_field_error(err, "Address", "Address is too long");
    }
    return 0;
}

int
freq_parse_timeout(const cJSON *json, uint32_t default_ms,
                   uint32_t *timeout_ms, struct freq_field_error *err)
{
    double seconds = 0;
    if (json_get_double(json, "TimeoutSeconds", &seconds) != 0) {
        return freq_field_error(err, "TimeoutSeconds",
                                "TimeoutSeconds must be a number");
    }
    if (seconds < 0) {
        return freq_field_error(err, "TimeoutSeconds",
                                "TimeoutSeconds cannot be negative");
    }
    if (seconds > 3600) {
        return freq_field_error(err, "TimeoutSeconds",
                                "TimeoutSeconds can be at most 3600");
    }
    *timeout_ms = seconds == 0 ? default_ms : (uint32_t)(seconds * 1000.0);
    return 0;
}

int
freq_check_keys(const cJSON *json, const char *const *allowed,
                struct freq_field_error *err)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, json)
    {
        const char *key = item->string != NULL ? item->string : "";
        if (strcmp(key, "Type") == 0 || strncmp(key, "_comment", 8) == 0) {
            continue;
        }
        bool known = false;
        for (size_t i = 0; allowed[i] != NULL && !known; i++) {
            known = strcmp(key, allowed[i]) == 0;
        }
        if (!known) {
            char shown[64];
            str_copy_clean(str_make(key, strlen(key) < 60 ? strlen(key) : 60),
                           shown, sizeof(shown));
            return freq_field_error(err, shown,
                                    "\"%s\" is not a setting "
                                    "for this source",
                                    shown);
        }
    }
    return 0;
}
