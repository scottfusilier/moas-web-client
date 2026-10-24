/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <math.h>
#include <string.h>

#include "core/caps.h"
#include "core/json.h"

_Static_assert(CJSON_NESTING_LIMIT == CAPS_JSON_DEPTH,
               "cJSON must be built with CJSON_NESTING_LIMIT = "
               "CAPS_JSON_DEPTH");

static bool
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int
json_parse(struct str text, cJSON **out)
{
    const char *end = NULL;

    if (text.len == 0) {
        return ERR_INVALID;
    }
    cJSON *root = cJSON_ParseWithLengthOpts(text.ptr, text.len, &end, 0);
    if (root == NULL) {
        return ERR_INVALID;
    }
    /* Nothing but whitespace may follow the value. */
    size_t used = (size_t)(end - text.ptr);
    for (size_t i = used; i < text.len; i++) {
        if (!is_space(text.ptr[i])) {
            cJSON_Delete(root);
            return ERR_INVALID;
        }
    }
    *out = root;
    return 0;
}

/* The member, or NULL if it is missing or null. */
static const cJSON *
member(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return item == NULL || cJSON_IsNull(item) ? NULL : item;
}

int
json_get_int(const cJSON *obj, const char *key, int min, int max, int *out)
{
    const cJSON *item = member(obj, key);
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsNumber(item)) {
        return ERR_INVALID;
    }
    double d = item->valuedouble;
    if (!isfinite(d) || d != floor(d)) {
        return ERR_INVALID;
    }
    if (d < (double)min || d > (double)max) {
        return ERR_RANGE;
    }
    *out = (int)d;
    return 0;
}

int
json_get_double(const cJSON *obj, const char *key, double *out)
{
    const cJSON *item = member(obj, key);
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return ERR_INVALID;
    }
    *out = item->valuedouble;
    return 0;
}

int
json_get_bool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = member(obj, key);
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsBool(item)) {
        return ERR_INVALID;
    }
    *out = cJSON_IsTrue(item);
    return 0;
}

int
json_get_text(const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = member(obj, key);
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return ERR_INVALID;
    }
    return str_copy_clean(str_from_cstr(item->valuestring), out, size);
}

int
json_get_array(const cJSON *obj, const char *key, size_t max,
               const cJSON **arr, size_t *count)
{
    const cJSON *item = member(obj, key);
    *arr = NULL;
    *count = 0;
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsArray(item)) {
        return ERR_INVALID;
    }
    int n = cJSON_GetArraySize(item);
    if (n < 0 || (size_t)n > max) {
        return ERR_LIMIT;
    }
    *arr = item;
    *count = (size_t)n;
    return 0;
}

int
json_get_object(const cJSON *obj, const char *key, const cJSON **out)
{
    const cJSON *item = member(obj, key);
    *out = NULL;
    if (item == NULL) {
        return 0;
    }
    if (!cJSON_IsObject(item)) {
        return ERR_INVALID;
    }
    *out = item;
    return 0;
}

int
json_write(struct buf *out, const cJSON *item)
{
    char *text = cJSON_PrintUnformatted(item);
    if (text == NULL) {
        return ERR_NOMEM;
    }
    int err = buf_append_cstr(out, text);
    cJSON_free(text);
    return err;
}
