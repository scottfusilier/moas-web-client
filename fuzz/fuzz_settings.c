/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * A settings file, as edited by hand.  Settings that are accepted must
 * come back the same after being written out and read again; a file that
 * is refused must leave the settings as they were.
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "app/settings.h"
#include "fuzz.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct str text = str_make((const char *)data, size);
    struct app_settings s;
    struct app_field_error err;
    bool radios = false;
    bool comments = false;

    app_settings_defaults(&s);
    if (app_settings_parse(text, &s, &radios, &comments, &err) != 0) {
        if (strcmp(s.server, "127.0.0.1:12059") != 0 || s.station != 1) {
            abort(); /* changed by a file that was refused */
        }
    } else {
        cJSON *json = app_settings_to_json(&s);
        char *written = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
        struct app_settings again;
        app_settings_defaults(&again);
        if (written != NULL &&
            (app_settings_parse(str_from_cstr(written), &again, &radios,
                                &comments, &err) != 0 ||
             !app_settings_same_sources(&s, &again) ||
             strcmp(s.server, again.server) != 0 ||
             strcmp(s.listen, again.listen) != 0))
        {
            abort(); /* written out, it does not read back the same */
        }
        app_settings_fini(&again);
        cJSON_free(written);
        cJSON_Delete(json);
    }
    app_settings_fini(&s);

    /* The settings view's reading of a file with a mistake in it. */
    app_settings_defaults(&s);
    app_settings_parse_partial(text, &s);
    app_settings_fini(&s);
    return 0;
}
