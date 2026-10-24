/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Reading and writing JSON through cJSON, with every value checked.
 *
 * The getters read one member of an object.  A member that is missing, or
 * null, leaves *out as it was and succeeds, so callers set defaults first.
 * A member of the wrong type gives ERR_INVALID, and a number outside the
 * allowed range ERR_RANGE.  Text is copied with str_copy_clean(), so it
 * never holds control characters or invalid UTF-8.
 */

#ifndef MOAS_CORE_JSON_H
#define MOAS_CORE_JSON_H

#include "cJSON.h"
#include "core/buf.h"
#include "core/common.h"
#include "core/str.h"

/*
 * Parses text, which must hold one JSON value and nothing else but
 * whitespace.  The caller frees *out with cJSON_Delete().  ERR_INVALID if
 * it is not JSON (or is nested deeper than CAPS_JSON_DEPTH).
 */
int json_parse(struct str text, cJSON **out);

/* An integer: a number with no fractional part, within min..max. */
int json_get_int(const cJSON *obj, const char *key, int min, int max,
                 int *out);

/* Any finite number. */
int json_get_double(const cJSON *obj, const char *key, double *out);

/* true or false. */
int json_get_bool(const cJSON *obj, const char *key, bool *out);

/* A string, cleaned; ERR_LIMIT if it does not fit in size bytes. */
int json_get_text(const cJSON *obj, const char *key, char *out, size_t size);

/*
 * An array of at most max elements.  A missing or null member gives an
 * empty array (*arr NULL, *count 0).
 */
int json_get_array(const cJSON *obj, const char *key, size_t max,
                   const cJSON **arr, size_t *count);

/* An object; a missing or null member gives *out NULL. */
int json_get_object(const cJSON *obj, const char *key, const cJSON **out);

/* Appends item to out as compact JSON. */
int json_write(struct buf *out, const cJSON *item);

#endif /* MOAS_CORE_JSON_H */
