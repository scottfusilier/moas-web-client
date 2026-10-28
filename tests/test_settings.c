/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The settings file: its format, checks, saving and reading, and how
 * moas-web-client starts with it.  Runs in its build folder, where it creates
 * and removes its own files.
 */

#include "core/common.h"

#include <stdlib.h>
#include <string.h>

#include "app/serve.h"
#include "app/settings.h"
#include "app/settings_file.h"
#include "core/json.h"
#include "platform/fs.h"
#include "test.h"

#define CHECK_TEXT(got, want) CHECK_MEM_EQ((got), strlen(got), (want))

static int
parse(const char *text, struct app_settings *s, struct app_field_error *err)
{
    bool radios = false;
    bool comments = false;
    return app_settings_parse(str_from_cstr(text), s, &radios, &comments, err);
}

static void
test_defaults(void)
{
    struct app_settings s;
    app_settings_defaults(&s);
    CHECK_TEXT(s.server, "127.0.0.1:12059");
    CHECK_TEXT(s.listen, "127.0.0.1:8080");
    CHECK_INT_EQ(s.station, 1);
    CHECK_INT_EQ(s.rows, 6);
    CHECK_INT_EQ(s.columns, 5);
    CHECK(s.show_txrx);
    CHECK_INT_EQ(s.radio_count, 2);
    CHECK_INT_EQ(s.radios[1].radio, 2);
    app_settings_fini(&s);
}

static void
test_full_file_and_comments(void)
{
    struct app_settings s;
    struct app_field_error err;
    bool radios = false;
    bool comments = false;

    app_settings_defaults(&s);
    /* Comments at any depth, more than once, as in a .moas file. */
    const char text[] =
        "{ \"_comment\": \"operating position 1\", \"_comment\": \"second\",\n"
        "  \"Server\": \" 192.168.1.10:12059 \", \"Station\": 2,\n"
        "  \"Radios\": [ { \"_comment\": \"run radio\", \"Radio\": 1,\n"
        "    \"Sources\": [ { \"_comment\": \"N1MM here\", \"Type\": \"n1mm\","
        " \"RadioNr\": 1 },\n"
        "                  { \"Type\": \"rigctld\", \"Address\": "
        "\"127.0.0.1:4533\" },\n"
        "                  { \"Type\": \"n3fjp\" } ] },\n"
        "    { \"Radio\": 3 } ],\n"
        "  \"RecentServers\": [\"192.168.1.10:12059\", \"10.0.0.2:12059\"] }";
    CHECK_INT_EQ(app_settings_parse(str_make(text, sizeof(text) - 1), &s,
                                    &radios, &comments, &err),
                 0);
    CHECK(radios && comments);
    CHECK_TEXT(s.server, "192.168.1.10:12059"); /* trimmed */
    CHECK_INT_EQ(s.station, 2);
    CHECK_INT_EQ(s.radio_count, 2);
    CHECK_INT_EQ(s.radios[0].source_count, 3);
    CHECK_TEXT(s.radios[0].sources[0].name, "N1MM radio 1");
    CHECK_TEXT(s.radios[0].sources[1].name, "rigctld 127.0.0.1:4533");
    CHECK_TEXT(s.radios[0].sources[2].name, "N3FJP 127.0.0.1:1100");
    CHECK_INT_EQ(s.radios[1].radio, 3);
    CHECK_INT_EQ(s.recent_count, 2);

    /* Written back with the defaults filled in, and no comments. */
    cJSON *json = app_settings_to_json(&s);
    char *out = cJSON_PrintUnformatted(json);
    CHECK(out != NULL);
    if (out != NULL) {
        CHECK(strstr(out, "_comment") == NULL);
        CHECK(strstr(out, "\"Version\":1") != NULL);
        CHECK(strstr(out, "\"ShowTXRX\":true") != NULL);
        CHECK(strstr(out, "{\"Type\":\"n1mm\",\"Address\":\"127.0.0.1:12060\","
                          "\"RadioNr\":1}") != NULL);
        CHECK(strstr(out, "{\"Type\":\"rigctld\",\"Address\":\"127.0.0.1:"
                          "4533\",\"TimeoutSeconds\":2,\"PollMilliseconds\":"
                          "250}") != NULL);
        CHECK(strstr(out, "{\"Type\":\"n3fjp\",\"Address\":\"127.0.0.1:1100\","
                          "\"PollMilliseconds\":2000}") != NULL);
        cJSON_free(out);
    }

    /* Reading it again gives the same settings. */
    struct app_settings again;
    app_settings_defaults(&again);
    CHECK_INT_EQ(app_settings_from_json(json, &again, &radios, &err), 0);
    CHECK(app_settings_same_sources(&s, &again));
    cJSON_Delete(json);
    app_settings_fini(&again);

    /* A copy is independent of the original. */
    struct app_settings copy;
    CHECK_INT_EQ(app_settings_copy(&copy, &s), 0);
    app_settings_fini(&s);
    CHECK_TEXT(copy.radios[0].sources[1].name, "rigctld 127.0.0.1:4533");
    app_settings_fini(&copy);
}

/* A file that leaves things out keeps the defaults for them. */
static void
test_partial_file(void)
{
    struct app_settings s;
    struct app_field_error err;
    bool radios = true;
    bool comments = true;

    app_settings_defaults(&s);
    CHECK_INT_EQ(app_settings_parse(STR_LIT("{\"Server\":\"10.0.0.2:12059\"}"),
                                    &s, &radios, &comments, &err),
                 0);
    CHECK(!radios && !comments);
    CHECK_TEXT(s.server, "10.0.0.2:12059");
    CHECK_TEXT(s.listen, "127.0.0.1:8080");
    CHECK_INT_EQ(s.radio_count, 2); /* the default radios */
    app_settings_fini(&s);
}

static void
check_problem(const char *text, const char *field, const char *message,
              int line)
{
    struct app_settings s;
    struct app_field_error err;
    app_settings_defaults(&s);
    int e = parse(text, &s, &err);
    test_check_int(e, ERR_INVALID, text, "ERR_INVALID", __FILE__, line);
    test_check_mem(err.field, strlen(err.field), field, strlen(field), text,
                   __FILE__, line);
    if (message != NULL) {
        test_check_mem(err.message, strlen(err.message), message,
                       strlen(message), text, __FILE__, line);
    }
    /* Untouched by a file that cannot be used. */
    test_check(strcmp(s.server, "127.0.0.1:12059") == 0, "server kept",
               __FILE__, line);
    app_settings_fini(&s);
}
#define CHECK_PROBLEM(text, field, message)                                   \
    check_problem((text), (field), (message), __LINE__)

static void
test_problems(void)
{
    CHECK_PROBLEM("not json", "", "the file is not valid JSON");
    CHECK_PROBLEM("[1]", "", "the settings must be a JSON object");
    CHECK_PROBLEM("{\"Sever\":\"127.0.0.1:12059\"}", "",
                  "\"Sever\" is not a setting");
    CHECK_PROBLEM(
        "{\"Version\":2}", "Version",
        "the file is Version 2, written by a newer moas-web-client; this "
        "one understands up to 1");
    CHECK_PROBLEM("{\"Version\":0}", "Version", NULL);
    CHECK_PROBLEM("{\"Server\":\"12059\"}", "Server",
                  "Server \"12059\" is not host:port with a numeric host, "
                  "for example 127.0.0.1:12059");
    CHECK_PROBLEM("{\"Listen\":\"moas.local:8080\"}", "Listen", NULL);
    CHECK_PROBLEM("{\"Station\":0}", "Station", "Station must be 1 or more");
    CHECK_PROBLEM("{\"Station\":\"one\"}", "Station",
                  "Station must be a whole number");
    CHECK_PROBLEM("{\"Rows\":0}", "Rows", "Rows must be 1 or more");
    CHECK_PROBLEM("{\"ShowTXRX\":\"yes\"}", "ShowTXRX",
                  "ShowTXRX must be true or false");
    CHECK_PROBLEM("{\"Columns\":1.5}", "Columns", NULL);
    CHECK_PROBLEM("{\"Radios\":[]}", "Radios",
                  "Radios must list at least one radio");
    CHECK_PROBLEM("{\"Radios\":[{\"Radio\":0}]}", "Radios.0.Radio", NULL);
    CHECK_PROBLEM("{\"Radios\":[{\"Radio\":1},{\"Radio\":1}]}",
                  "Radios.1.Radio", "radio 1 is listed twice");
    CHECK_PROBLEM("{\"Radios\":[{\"Radio\":1,\"Sorces\":[]}]}", "Radios.0",
                  "\"Sorces\" is not a setting in Radios.0");
    CHECK_PROBLEM("{\"Radios\":[{\"Radio\":1,\"Sources\":[{\"Type\":"
                  "\"rigctld\",\"Address\":\"4532\"}]}]}",
                  "Radios.0.Sources.0.Address", NULL);
    CHECK_PROBLEM("{\"Radios\":[{\"Radio\":2,\"Sources\":[{\"Type\":\"n1mm\"},"
                  "{\"Type\":\"flrig\"}]}]}",
                  "Radios.0.Sources.1.Type",
                  "radio 2, source 2: Type \"flrig\" is not a known source; "
                  "use \"n1mm\", \"dxlog\", \"rigctld\" or \"n3fjp\"");
}

/* The example shipped with moas-web-client is a good file. */
static void
test_example_file(void)
{
    struct app_settings s;
    struct app_settings_file info;
    struct app_field_error err;

    app_settings_defaults(&s);
    CHECK_INT_EQ(app_settings_load(EXAMPLE_SETTINGS, &s, &info, &err), 0);
    CHECK(info.had_comments && info.has_radios);
    CHECK_INT_EQ(s.radio_count, 2);
    CHECK_INT_EQ(s.radios[0].source_count, 3);
    CHECK_TEXT(s.radios[0].sources[2].name, "N3FJP 127.0.0.1:1100");
    const struct app_radio_settings *r = &s.radios[1];
    CHECK_INT_EQ(r->radio, 2);
    CHECK_INT_EQ(r->source_count, 2);
    CHECK_TEXT(r->sources[0].name, "N1MM radio 2");
    CHECK_TEXT(r->sources[1].name, "rigctld 127.0.0.1:4533");
    app_settings_fini(&s);
}

/* Mistakes in a source, found by the source's own checks. */
static void
test_source_problems(void)
{
    const char *bad[] = {
        "{\"Type\":\"n1mm\",\"PollMilliseconds\":100}",
        "{\"Type\":\"rigctld\",\"PollMilliseconds\":10}",
        "{\"Type\":\"rigctld\",\"TimeoutSeconds\":-1}",
        "{\"Type\":\"rigctld\",\"RadioNr\":2}",
        "{\"Type\":\"n1mm\",\"Address\":\"12060\"}",
        "{\"Type\":\"n3fjp\",\"PollMilliseconds\":10}",
        "{\"Type\":\"n3fjp\",\"FollowAntenna\":true}",
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        char text[256];
        struct app_settings s;
        struct app_field_error err;
        snprintf(text, sizeof(text),
                 "{\"Radios\":[{\"Radio\":1,\"Sources\":[%s]}]}", bad[i]);
        app_settings_defaults(&s);
        test_check_int(parse(text, &s, &err), ERR_INVALID, bad[i],
                       "ERR_INVALID", __FILE__, __LINE__);
        test_check(strncmp(err.field, "Radios.0.Sources.0.", 19) == 0,
                   err.field, __FILE__, __LINE__);
        app_settings_fini(&s);
    }
}

/* A file with a mistake: shown with what could be read. */
static void
test_partial_reading(void)
{
    struct app_settings s;
    app_settings_defaults(&s);
    app_settings_parse_partial(
        STR_LIT("{\"Server\":\"10.0.0.3:12059\",\"Station\":0}"), &s);
    CHECK_TEXT(s.server, "10.0.0.3:12059");
    app_settings_fini(&s);
}

static void
test_recent_servers_and_sources(void)
{
    struct app_settings s;
    app_settings_defaults(&s);
    for (int i = 0; i < 8; i++) {
        char server[32];
        snprintf(server, sizeof(server), "10.0.0.%d:12059", i);
        app_settings_remember_server(&s, server);
    }
    CHECK_INT_EQ(s.recent_count, APP_RECENT_SERVERS);
    CHECK_TEXT(s.recent[0], "10.0.0.7:12059");
    app_settings_remember_server(&s, "10.0.0.5:12059"); /* moves up */
    CHECK_TEXT(s.recent[0], "10.0.0.5:12059");
    CHECK_TEXT(s.recent[1], "10.0.0.7:12059");
    CHECK_INT_EQ(s.recent_count, APP_RECENT_SERVERS);

    struct app_settings other;
    app_settings_defaults(&other);
    CHECK(app_settings_same_sources(&s, &other));
    other.station = 3;
    CHECK(!app_settings_same_sources(&s, &other));
    app_settings_fini(&s);
    app_settings_fini(&other);
}

#define FILE_A "test_settings_a.json"
#define NESTED "test_settings_dir/config/moas-web-client/moas-web-client.json"

static void
test_file_save_and_load(void)
{
    struct app_settings s;
    struct app_settings_file info;
    struct app_field_error err;
    char version[SHA256_HEX_SIZE];

    plat_remove_file(FILE_A);
    app_settings_defaults(&s);
    CHECK_INT_EQ(app_settings_load(FILE_A, &s, &info, &err), ERR_NOT_FOUND);
    CHECK(!info.exists);
    CHECK_INT_EQ(app_settings_version(FILE_A, version), 0);
    CHECK_TEXT(version, "");

    /* Saving creates the folders it needs, and writes the Version. */
    snprintf(s.server, sizeof(s.server), "10.0.0.9:12059");
    plat_addr_parse(s.server, strlen(s.server), &s.server_addr);
    CHECK_INT_EQ(app_settings_save(NESTED, &s, &err), 0);
    char *data = NULL;
    size_t len = 0;
    CHECK_INT_EQ(plat_read_file(NESTED, 100000, &data, &len), 0);
    if (data != NULL) {
        CHECK(strstr(data, "\"Version\": 1,") != NULL);
        CHECK(strstr(data, "    \"Server\": \"10.0.0.9:12059\"") != NULL);
        CHECK(strchr(data, '\t') == NULL);
        free(data);
    }
    struct app_settings back;
    app_settings_defaults(&back);
    CHECK_INT_EQ(app_settings_load(NESTED, &back, &info, &err), 0);
    CHECK(info.exists && info.version[0] != '\0' && !info.had_comments);
    CHECK_TEXT(back.server, "10.0.0.9:12059");
    CHECK_INT_EQ(app_settings_version(NESTED, version), 0);
    CHECK_MEM_EQ(version, strlen(version), info.version);
    app_settings_fini(&back);

    /* A read-only file is refused, and left as it was. */
    CHECK_INT_EQ(plat_set_read_only(NESTED, true), 0);
    snprintf(s.server, sizeof(s.server), "10.0.0.8:12059");
    plat_addr_parse(s.server, strlen(s.server), &s.server_addr);
    CHECK_INT_EQ(app_settings_save(NESTED, &s, &err), ERR_ACCESS);
    CHECK(strstr(err.message, "is read-only for this user") != NULL);
    char after[SHA256_HEX_SIZE];
    app_settings_version(NESTED, after);
    CHECK_MEM_EQ(after, strlen(after), version);
    plat_set_read_only(NESTED, false);
    plat_remove_file(NESTED);
    app_settings_fini(&s);
}

/* Starting up: a missing or broken file is not fatal. */
static void
test_startup(void)
{
    struct app_args args;
    char error[256];
    char message[320];
    struct app_settings s;
    char *argv[] = {(char *)(uintptr_t)"moas-web-client"};

    app_args_parse(1, argv, &args, error, sizeof(error));

    const char *broken[] = {"{\"Server\": \"192.168.1.10:12059\",",
                            "{\"Server\": \"192.168.1.10:12059\", "
                            "\"Station\": 0}"};
    for (size_t i = 0; i < ARRAY_LEN(broken); i++) {
        plat_replace_file(FILE_A, broken[i], strlen(broken[i]));
        CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                          sizeof(message)),
                     0);
        CHECK_TEXT(s.server, "127.0.0.1:12059"); /* the defaults */
        app_settings_fini(&s);
        /* The file is left alone. */
        char *data = NULL;
        size_t len = 0;
        plat_read_file(FILE_A, 1000, &data, &len);
        CHECK(data != NULL && strcmp(data, broken[i]) == 0);
        free(data);
    }

    /* A good file, then command-line options over it. */
    const char good[] = "{\"Server\": \"192.168.1.10:12059\", \"Radios\": "
                        "[{\"Radio\": 2}]}";
    plat_replace_file(FILE_A, good, sizeof(good) - 1);
    CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                      sizeof(message)),
                 0);
    CHECK_TEXT(s.server, "192.168.1.10:12059");
    CHECK_INT_EQ(s.radio_count, 1);
    app_settings_fini(&s);

    char *over[] = {(char *)(uintptr_t)"moas-web-client",
                    (char *)(uintptr_t)"-server",
                    (char *)(uintptr_t)"10.0.0.4:12059",
                    (char *)(uintptr_t)"-station", (char *)(uintptr_t)"3"};
    app_args_parse(5, over, &args, error, sizeof(error));
    CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                      sizeof(message)),
                 0);
    CHECK_TEXT(s.server, "10.0.0.4:12059");
    CHECK_INT_EQ(s.station, 3);
    CHECK_INT_EQ(s.radio_count, 1);
    app_settings_fini(&s);

    /* -radios cannot override a file that lists radios. */
    char *radios[] = {(char *)(uintptr_t)"moas-web-client",
                      (char *)(uintptr_t)"-radios", (char *)(uintptr_t)"1,3"};
    app_args_parse(3, radios, &args, error, sizeof(error));
    CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                      sizeof(message)),
                 ERR_INVALID);
    CHECK(strstr(message, "-radios cannot be used") != NULL);
    app_settings_fini(&s);
    /* But without the file it gives the radios. */
    plat_remove_file(FILE_A);
    CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                      sizeof(message)),
                 0);
    CHECK_INT_EQ(s.radio_count, 2);
    CHECK_INT_EQ(s.radios[1].radio, 3);
    app_settings_fini(&s);

    /* A bad command line is reported. */
    char *bad[] = {(char *)(uintptr_t)"moas-web-client",
                   (char *)(uintptr_t)"-server", (char *)(uintptr_t)"x"};
    app_args_parse(3, bad, &args, error, sizeof(error));
    CHECK_INT_EQ(app_startup_settings(FILE_A, &args, true, &s, message,
                                      sizeof(message)),
                 ERR_INVALID);
    CHECK(strstr(message, "command line: Server \"x\"") != NULL);
    app_settings_fini(&s);
}

int
main(void)
{
    RUN(test_defaults);
    RUN(test_full_file_and_comments);
    RUN(test_partial_file);
    RUN(test_problems);
    RUN(test_example_file);
    RUN(test_source_problems);
    RUN(test_partial_reading);
    RUN(test_recent_servers_and_sources);
    RUN(test_file_save_and_load);
    RUN(test_startup);
    return test_summary();
}
