/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/settings_file.h"
#include "core/buf.h"
#include "platform/fs.h"

static int
join(char *out, size_t size, const char *dir, const char *a, const char *b)
{
    char sep = plat_path_separator();
    int n = b != NULL ? snprintf(out, size, "%s%c%s%c%s", dir, sep, a, sep, b)
                      : snprintf(out, size, "%s%c%s", dir, sep, a);
    return n < 0 || (size_t)n >= size ? ERR_LIMIT : 0;
}

int
app_settings_find(const char *config, const char *argv0, char *out,
                  size_t size)
{
    if (config != NULL && config[0] != '\0') {
        int n = snprintf(out, size, "%s", config);
        return n < 0 || (size_t)n >= size ? ERR_LIMIT : 0;
    }
    char dir[CAPS_PATH];
    if (plat_exe_dir(argv0, dir, sizeof(dir)) == 0 &&
        join(out, size, dir, APP_SETTINGS_FILE_NAME, NULL) == 0)
    {
        bool writable;
        if (plat_file_writable(out, &writable) != ERR_NOT_FOUND) {
            return 0; /* there is one beside the program */
        }
    }
    if (plat_settings_dir(dir, sizeof(dir)) == 0) {
        return join(out, size, dir, "moas-web-client", APP_SETTINGS_FILE_NAME);
    }
    /* No settings folder (no home folder): the current folder, then. */
    int n = snprintf(out, size, "%s", APP_SETTINGS_FILE_NAME);
    return n < 0 || (size_t)n >= size ? ERR_LIMIT : 0;
}

static int
read_text(const char *path, char **data, size_t *len)
{
    return plat_read_file(path, CAPS_SETTINGS_FILE, data, len);
}

int
app_settings_version(const char *path, char out[SHA256_HEX_SIZE])
{
    char *data = NULL;
    size_t len = 0;

    out[0] = '\0';
    int err = read_text(path, &data, &len);
    if (err == ERR_NOT_FOUND) {
        return 0;
    }
    if (err != 0) {
        return err;
    }
    sha256_hex(data, len, out);
    free(data);
    return 0;
}

int
app_settings_load(const char *path, struct app_settings *s,
                  struct app_settings_file *info, struct app_field_error *err)
{
    char *data = NULL;
    size_t len = 0;

    memset(info, 0, sizeof(*info));
    memset(err, 0, sizeof(*err));
    int e = read_text(path, &data, &len);
    if (e == ERR_NOT_FOUND) {
        return e;
    }
    if (e != 0) {
        snprintf(err->message, sizeof(err->message), "cannot read %s: %s",
                 path, e == ERR_LIMIT ? "the file is too large" : err_str(e));
        return e;
    }
    info->exists = true;
    sha256_hex(data, len, info->version);
    e = app_settings_parse(str_make(data, len), s, &info->has_radios,
                           &info->had_comments, err);
    free(data);
    return e;
}

void
app_settings_load_partial(const char *path, struct app_settings *s)
{
    char *data = NULL;
    size_t len = 0;
    if (read_text(path, &data, &len) == 0) {
        app_settings_parse_partial(str_make(data, len), s);
        free(data);
    }
}

/* Creates every folder in path above its last component. */
static int
make_parent_dirs(const char *path)
{
    char dir[CAPS_PATH];
    size_t n = strlen(path);
    if (n >= sizeof(dir)) {
        return ERR_LIMIT;
    }
    memcpy(dir, path, n + 1);
    char *last = strrchr(dir, plat_path_separator());
    if (plat_path_separator() == '\\') {
        /* Windows accepts '/' too. */
        char *other = strrchr(dir, '/');
        if (other != NULL && (last == NULL || other > last)) {
            last = other;
        }
    }
    if (last == NULL || last == dir) {
        return 0;
    }
    *last = '\0';
    /* Each folder from the top down; existing ones are fine. */
    for (char *p = dir + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char keep = *p;
            *p = '\0';
            /* Skip "C:" on Windows: a drive, not a folder. */
            if (!(p == dir + 2 && dir[1] == ':')) {
                plat_make_dir(dir);
            }
            *p = keep;
        }
    }
    return plat_make_dir(dir);
}

int
app_settings_save(const char *path, const struct app_settings *s,
                  struct app_field_error *err)
{
    memset(err, 0, sizeof(*err));

    bool writable = true;
    int e = plat_file_writable(path, &writable);
    if (e == 0 && !writable) {
        snprintf(err->message, sizeof(err->message),
                 "%s is read-only for this user: change it by hand (for "
                 "example as root) or make it writable",
                 path);
        return ERR_ACCESS;
    }

    cJSON *json = app_settings_to_json(s);
    char *text = json != NULL ? cJSON_Print(json) : NULL;
    cJSON_Delete(json);
    if (text == NULL) {
        snprintf(err->message, sizeof(err->message), "out of memory");
        return ERR_NOMEM;
    }
    /*
     * cJSON indents with tabs and puts a tab after each ':'; write four
     * spaces and one space instead.  Tabs inside
     * strings are escaped by cJSON, so every raw tab is layout.
     */
    struct buf out;
    buf_init(&out, CAPS_SETTINGS_FILE);
    e = 0;
    char prev = '\0';
    for (const char *p = text; *p != '\0' && e == 0; p++) {
        if (*p == '\t') {
            e = buf_append_cstr(&out, prev == ':' ? " " : "    ");
        } else {
            e = buf_append(&out, p, 1);
        }
        prev = *p;
    }
    cJSON_free(text);
    e = e == 0 ? buf_append_cstr(&out, "\n") : e;
    if (e != 0) {
        buf_fini(&out);
        snprintf(err->message, sizeof(err->message),
                 "the settings are too large to save");
        return e;
    }

    e = make_parent_dirs(path);
    if (e != 0) {
        buf_fini(&out);
        snprintf(err->message, sizeof(err->message),
                 "cannot create the folder for %s: %s", path, err_str(e));
        return e;
    }
    e = plat_replace_file(path, out.data, out.len);
    buf_fini(&out);
    if (e != 0) {
        snprintf(err->message, sizeof(err->message), "cannot write %s: %s",
                 path, err_str(e));
    }
    return e;
}
