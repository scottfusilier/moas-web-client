/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Finding, reading and saving the settings file. */

#ifndef MOAS_APP_SETTINGS_FILE_H
#define MOAS_APP_SETTINGS_FILE_H

#include "app/settings.h"
#include "core/caps.h"
#include "core/sha256.h"

#define APP_SETTINGS_FILE_NAME "moas-web-client.json"

/*
 * Which settings file to use: config if given (-config); else
 * moas-web-client.json in the program's folder, if there is one there (handy
 * for a folder on a USB stick); else moas-web-client.json in the user's
 * settings folder, such as
 * ~/.config/moas-web-client/ or %AppData%\moas-web-client\, where a new file
 * is created. The same on every system.
 */
int app_settings_find(const char *config, const char *argv0, char *out,
                      size_t size);

/* What reading the file found. */
struct app_settings_file {
    bool exists;
    bool has_radios;   /* the file lists radios */
    bool had_comments; /* it has "_comment" entries, which saving drops */
    char version[SHA256_HEX_SIZE]; /* of its contents; "" if no file */
};

/*
 * Reads the file at path over *s (the defaults).  ERR_NOT_FOUND if there
 * is none (info->exists false); ERR_INVALID with err filled in if it
 * cannot be used; another error if it cannot be read.  On any error *s is
 * left as it was.
 */
int app_settings_load(const char *path, struct app_settings *s,
                      struct app_settings_file *info,
                      struct app_field_error *err);

/* As much of the file as is usable, over *s; see app_settings_parse_partial.
 */
void app_settings_load_partial(const char *path, struct app_settings *s);

/* The version of the file's contents now, "" if there is no file. */
int app_settings_version(const char *path, char out[SHA256_HEX_SIZE]);

/*
 * Saves s to path, replacing the file in one step and creating its folder
 * if need be.  A file this user may not write is refused rather than
 * replaced.  On failure, err->message says why.
 */
int app_settings_save(const char *path, const struct app_settings *s,
                      struct app_field_error *err);

#endif /* MOAS_APP_SETTINGS_FILE_H */
