/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* N3FJP's API lines, the n3fjp source, and the one-off Test. */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "fake_n3fjp.h"
#include "freq/n3fjp.h"
#include "freq/n3fjp_reply.h"
#include "freq/source.h"
#include "platform/clock.h"
#include "test.h"

static void
test_lines(void)
{
    int khz = 0;

    /* As Amateur Contact Log sends them. */
    CHECK_INT_EQ(n3fjp_parse_line(
                     STR_LIT("<CMD><READBMFRESPONSE><BAND>80</BAND><MODE>CW"
                             "</MODE><MODETEST>CW</MODETEST><FREQ>3.537000"
                             "</FREQ></CMD>\r"),
                     &khz),
                 0);
    CHECK_INT_EQ(khz, 3537);
    CHECK_INT_EQ(n3fjp_parse_line(
                     STR_LIT("<CMD><READBMFRESPONSE><BAND>80</BAND><MODE>"
                             "</MODE><MODETEST>PH</MODETEST><FREQ>21.027105"
                             "</FREQ></CMD>"),
                     &khz),
                 0);
    CHECK_INT_EQ(khz, 21027);

    /* Other ways of writing MHz; decimals past kHz are dropped. */
    struct {
        const char *freq;
        int khz;
    } good[] = {
        {"7.22802", 7228},       {"14", 14000}, {"14.0259999999", 14025},
        {" 1.8 ", 1800},         {"0.001", 1},  {"10368.1", 10368100},
        {"1000000", 1000000000},
    };
    for (size_t i = 0; i < ARRAY_LEN(good); i++) {
        char line[128];
        snprintf(line, sizeof(line),
                 "<CMD><READBMFRESPONSE><FREQ>%s</FREQ></CMD>", good[i].freq);
        khz = 0;
        int err = n3fjp_parse_line(str_from_cstr(line), &khz);
        test_check_int(err, 0, line, "0", __FILE__, __LINE__);
        test_check_int(khz, good[i].khz, line, "kHz", __FILE__, __LINE__);
    }

    /* No usable frequency. */
    const char *none[] = {"",
                          "0",
                          "0.000000",
                          "0.000999",
                          "1000000.000001",
                          "99999999999999999999"};
    for (size_t i = 0; i < ARRAY_LEN(none); i++) {
        char line[128];
        snprintf(line, sizeof(line),
                 "<CMD><READBMFRESPONSE><FREQ>%s</FREQ></CMD>", none[i]);
        int err = n3fjp_parse_line(str_from_cstr(line), &khz);
        test_check_int(err, ERR_RANGE, line, "ERR_RANGE", __FILE__, __LINE__);
    }

    /* Lines that are not readings, and are ignored. */
    const char *ignored[] = {
        "",
        "\r",
        "<CMD><CHANGEFREQ><VALUE>14.025</VALUE></CMD>",
        "<CMD><RIGSWAPEVENT><RIG>2</RIG></CMD>",
        "<CMD></CMD>",
    };
    for (size_t i = 0; i < ARRAY_LEN(ignored); i++) {
        int err = n3fjp_parse_line(str_from_cstr(ignored[i]), &khz);
        test_check_int(err, ERR_NOT_FOUND, ignored[i], "ERR_NOT_FOUND",
                       __FILE__, __LINE__);
    }

    /* Nonsense. */
    const char *bad[] = {
        "garbage",
        "<CMD>",
        "<CMD><READBMFRESPONSE><FREQ>14.025</FREQ>",
        "<CMD><READBMFRESPONSE><BAND>20</BAND></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>14.025</CMD>",
        "<CMD><READBMFRESPONSE><FREQ>-3.5</FREQ></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>3.5x</FREQ></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>3,5</FREQ></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>.5</FREQ></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>1.2.3</FREQ></CMD>",
        "<CMD><READBMFRESPONSE><FREQ>14.025e0</FREQ></CMD>",
        "<cmd><readbmfresponse><freq>14.025</freq></cmd>",
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        int err = n3fjp_parse_line(str_from_cstr(bad[i]), &khz);
        test_check_int(err, ERR_INVALID, bad[i], "ERR_INVALID", __FILE__,
                       __LINE__);
    }
}

static int
start_source(struct freq_env *env, const struct plat_addr *addr,
             const char *extra, struct freq_tracker *t,
             struct freq_source **out)
{
    char text[PLAT_ADDR_TEXT_MAX];
    char json[256];
    cJSON *j = NULL;
    struct freq_source_config c;
    struct freq_field_error err;
    struct freq_feed feed;

    plat_addr_format(addr, text);
    snprintf(json, sizeof(json), "{\"Type\":\"n3fjp\",\"Address\":\"%s\"%s}",
             text, extra);
    int e = json_parse(str_from_cstr(json), &j);
    if (e == 0) {
        e = freq_parse_source(j, 1, &c, &err);
        cJSON_Delete(j);
    }
    if (e != 0) {
        return e;
    }
    e = freq_tracker_add(t, c.name, c.timeout_ms, &feed);
    if (e == 0) {
        e = freq_source_start(env, &c, feed, out);
    }
    freq_source_config_fini(&c);
    return e;
}

/* Runs the loop until the tracker shows want (or an error, if want_error). */
static bool
run_until_state(struct loop *loop, struct freq_tracker *t, int want_khz,
                bool want_error, const char *error_part, int timeout_ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)timeout_ms;
    while (plat_monotonic_ms() < end) {
        loop_run_once(loop, 20);
        struct freq_status s;
        freq_tracker_status(t, &s, 1);
        if (want_error && s.state == FREQ_ERROR &&
            (error_part == NULL || strstr(s.error, error_part) != NULL))
        {
            return true;
        }
        if (!want_error && s.state == FREQ_UP && s.khz == want_khz) {
            return true;
        }
    }
    return false;
}

/* Runs the loop for ms, then says whether the tracker still shows khz. */
static bool
stays_at(struct loop *loop, struct freq_tracker *t, int khz, int ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)ms;
    while (plat_monotonic_ms() < end) {
        loop_run_once(loop, 20);
    }
    struct freq_status s;
    freq_tracker_status(t, &s, 1);
    return s.state == FREQ_UP && s.khz == khz;
}

static void
test_source(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *t;
    struct freq_source *s;
    struct fake_n3fjp f;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    fake_n3fjp_start(&f, loop, "14.025000");
    CHECK_INT_EQ(
        start_source(env, &f.addr, ",\"PollMilliseconds\":1000", t, &s), 0);

    CHECK(run_until_state(loop, t, 14025, false, NULL, 3000));
    CHECK_MEM_EQ(f.request, strlen(f.request), "<CMD><READBMF></CMD>\r");
    fake_n3fjp_set_freq(&f, "7.228020");
    CHECK(run_until_state(loop, t, 7228, false, NULL, 3000));
    /* It keeps polling, and the reading never goes stale in between. */
    int before = f.requests;
    CHECK(stays_at(loop, t, 7228, 2500));
    CHECK(f.requests >= before + 2);

    /* No frequency: shown, then it reconnects and recovers. */
    fake_n3fjp_set_freq(&f, "");
    CHECK(run_until_state(loop, t, 0, true, "N3FJP at 127.0.0.1:", 3000));
    CHECK(run_until_state(loop, t, 0, true, "no usable frequency", 3000));
    fake_n3fjp_set_freq(&f, "3.525000");
    CHECK(run_until_state(loop, t, 3525, false, NULL, 5000));
    CHECK(f.accepted >= 2);

    /* Nonsense, and no answer at all. */
    snprintf(f.reply, sizeof(f.reply), "hello\x1b[2J\r\n");
    CHECK(run_until_state(loop, t, 0, true, "unexpected reply \"hello?[2J\"",
                          3000));
    f.silent = true;
    fake_n3fjp_set_freq(&f, "3.525000");
    CHECK(run_until_state(loop, t, 0, true, "no reply", 8000));

    /* N3FJP exits: shown, and picked up again when it is back. */
    f.silent = false;
    CHECK(run_until_state(loop, t, 3525, false, NULL, 5000));
    int accepted = f.accepted;
    fake_n3fjp_hang_up(&f);
    CHECK(run_until_state(loop, t, 0, true, "connection closed", 3000));
    CHECK(run_until_state(loop, t, 3525, false, NULL, 5000));
    CHECK_INT_EQ(f.accepted, accepted + 1);

    freq_source_stop(s);
    fake_n3fjp_stop(&f);
    freq_tracker_destroy(t);
    freq_env_destroy(env);
    loop_destroy(loop);
}

/* What N3FJP sends unasked: changes, copies of replies, other commands. */
static void
test_unasked(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *t;
    struct freq_source *s;
    struct fake_n3fjp f;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    fake_n3fjp_start(&f, loop, "3.537000");
    /* Asks once, then not again for a minute. */
    CHECK_INT_EQ(start_source(env, &f.addr,
                              ",\"PollMilliseconds\":60000,"
                              "\"TimeoutSeconds\":3600",
                              t, &s),
                 0);
    CHECK(run_until_state(loop, t, 3537, false, NULL, 3000));
    CHECK_INT_EQ(f.requests, 1);

    /* A change, sent unasked. */
    fake_n3fjp_send(&f, "<CMD><READBMFRESPONSE><BAND>80</BAND><MODE>CW"
                        "</MODE><MODETEST>CW</MODETEST><FREQ>3.533986"
                        "</FREQ></CMD>\r\n");
    CHECK(run_until_state(loop, t, 3533, false, NULL, 3000));

    /* Two at once, as with a band change: the last one counts. */
    fake_n3fjp_send(&f, "<CMD><READBMFRESPONSE><BAND>80</BAND><MODE>CW"
                        "</MODE><MODETEST>CW</MODETEST><FREQ>21.027105"
                        "</FREQ></CMD>\r\n"
                        "<CMD><READBMFRESPONSE><BAND>15</BAND><MODE>CW"
                        "</MODE><MODETEST>CW</MODETEST><FREQ>21.028000"
                        "</FREQ></CMD>\r\n");
    CHECK(run_until_state(loop, t, 21028, false, NULL, 3000));

    /* One split across sends. */
    fake_n3fjp_send(&f, "<CMD><READBMFRESPONSE><BAND>20</BAND><MO");
    CHECK(stays_at(loop, t, 21028, 200));
    fake_n3fjp_send(&f, "DE>CW</MODE><MODETEST>CW</MODETEST><FREQ>14.0");
    CHECK(stays_at(loop, t, 21028, 200));
    fake_n3fjp_send(&f, "25000</FREQ></CMD>\r\n");
    CHECK(run_until_state(loop, t, 14025, false, NULL, 3000));

    /* Other commands and blank lines change nothing. */
    fake_n3fjp_send(&f, "<CMD><CHANGEFREQ><VALUE>28.400</VALUE></CMD>\r\n"
                        "\r\n"
                        "<CMD><RIGSWAPEVENT><RIG>2</RIG></CMD>\r\n");
    CHECK(stays_at(loop, t, 14025, 300));

    /* A line too long to keep is dropped, and what follows still counts. */
    char chunk[600];
    memset(chunk, 'x', sizeof(chunk) - 1);
    chunk[sizeof(chunk) - 1] = '\0';
    memcpy(chunk, "<CMD><UPDATERESPONSE>", 21);
    for (int i = 0; i < 4; i++) {
        fake_n3fjp_send(&f, chunk);
        loop_run_once(loop, 20);
    }
    fake_n3fjp_send(&f, "</CMD>\r\n");
    CHECK(stays_at(loop, t, 14025, 300));
    fake_n3fjp_send(&f, "<CMD><READBMFRESPONSE><FREQ>7.001000</FREQ></CMD>"
                        "\r\n");
    CHECK(run_until_state(loop, t, 7001, false, NULL, 3000));
    CHECK_INT_EQ(f.accepted, 1);
    CHECK_INT_EQ(f.requests, 1);

    freq_source_stop(s);
    fake_n3fjp_stop(&f);
    freq_tracker_destroy(t);
    freq_env_destroy(env);
    loop_destroy(loop);
}

static void
test_not_running(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *t;
    struct freq_source *s;
    struct plat_addr addr;
    plat_socket l;

    plat_addr_parse("127.0.0.1:0", 11, &addr);
    plat_tcp_listen(&addr, &l);
    plat_socket_local_addr(l, &addr);
    plat_socket_close(l);

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    CHECK_INT_EQ(start_source(env, &addr, "", t, &s), 0);
    /* Windows takes about two seconds to report a refusal. */
    CHECK(run_until_state(loop, t, 0, true, "connection refused", 10000));

    freq_source_stop(s);
    freq_tracker_destroy(t);
    freq_env_destroy(env);
    loop_destroy(loop);
}

struct test_result {
    bool done;
    int err;
    int khz;
    char message[CAPS_FREQ_ERROR];
};

static void
on_test_done(int err, int khz, const char *message, void *arg)
{
    struct test_result *r = arg;
    r->done = true;
    r->err = err;
    r->khz = khz;
    snprintf(r->message, sizeof(r->message), "%s", message);
}

static void
test_one_off(void)
{
    struct loop *loop;
    struct fake_n3fjp f;
    struct freq_n3fjp_test *test;
    struct test_result r = {0};

    CHECK_INT_EQ(loop_create(&loop), 0);
    fake_n3fjp_start(&f, loop, "28.074000");
    CHECK_INT_EQ(freq_n3fjp_test(loop, &f.addr, on_test_done, &r, &test), 0);
    for (int i = 0; i < 150 && !r.done; i++) {
        loop_run_once(loop, 20);
    }
    CHECK(r.done);
    CHECK_INT_EQ(r.err, 0);
    CHECK_INT_EQ(r.khz, 28074);

    /* A failing test reports why, once. */
    struct test_result r2 = {0};
    fake_n3fjp_set_freq(&f, "0.000000");
    CHECK_INT_EQ(freq_n3fjp_test(loop, &f.addr, on_test_done, &r2, &test), 0);
    for (int i = 0; i < 150 && !r2.done; i++) {
        loop_run_once(loop, 20);
    }
    CHECK(r2.done && r2.err != 0);
    CHECK(strstr(r2.message, "no usable frequency") != NULL);

    /* A cancelled test never calls back. */
    struct test_result r3 = {0};
    CHECK_INT_EQ(freq_n3fjp_test(loop, &f.addr, on_test_done, &r3, &test), 0);
    freq_n3fjp_test_cancel(test);
    for (int i = 0; i < 20; i++) {
        loop_run_once(loop, 10);
    }
    CHECK(!r3.done);

    fake_n3fjp_stop(&f);
    loop_destroy(loop);
}

static void
test_settings(void)
{
    struct freq_source_config c;
    struct freq_field_error err;
    cJSON *j = NULL;

    /*
     * The defaults, and how the timeout follows the poll unless it is set;
     * it is written back only if set.
     */
    struct {
        const char *json;
        uint32_t timeout_ms;
        const char *written;
    } good[] = {
        {"{\"Type\":\"n3fjp\"}", 5000,
         "{\"Type\":\"n3fjp\",\"Address\":\"127.0.0.1:1100\","
         "\"PollMilliseconds\":2000}"},
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":10000}", 13000,
         "{\"Type\":\"n3fjp\",\"Address\":\"127.0.0.1:1100\","
         "\"PollMilliseconds\":10000}"},
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":1000,\"TimeoutSeconds\":"
         "10}",
         10000,
         "{\"Type\":\"n3fjp\",\"Address\":\"127.0.0.1:1100\","
         "\"PollMilliseconds\":1000,\"TimeoutSeconds\":10}"},
    };
    for (size_t i = 0; i < ARRAY_LEN(good); i++) {
        j = NULL;
        json_parse(str_from_cstr(good[i].json), &j);
        int e = freq_parse_source(j, 3, &c, &err);
        cJSON_Delete(j);
        test_check_int(e, 0, good[i].json, "0", __FILE__, __LINE__);
        if (e != 0) {
            continue;
        }
        CHECK_MEM_EQ(c.name, strlen(c.name), "N3FJP 127.0.0.1:1100");
        test_check_int((int)c.timeout_ms, (int)good[i].timeout_ms,
                       good[i].json, "timeout", __FILE__, __LINE__);
        cJSON *out = freq_source_to_json(&c);
        char *text = out != NULL ? cJSON_PrintUnformatted(out) : NULL;
        CHECK(text != NULL);
        if (text != NULL) {
            test_check_mem(text, strlen(text), good[i].written,
                           strlen(good[i].written), good[i].json, __FILE__,
                           __LINE__);
        }
        cJSON_free(text);
        cJSON_Delete(out);
        freq_source_config_fini(&c);
    }

    struct {
        const char *json;
        const char *message;
    } bad[] = {
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":500}",
         "PollMilliseconds must be at least 1000 (and at most 60000)"},
        {"{\"Type\":\"n3fjp\",\"TimeoutSeconds\":2}",
         "TimeoutSeconds must be at least 5 when PollMilliseconds is 2000"},
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":5000,"
         "\"TimeoutSeconds\":7.5}",
         "TimeoutSeconds must be at least 8 when PollMilliseconds is 5000"},
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":-1}",
         "PollMilliseconds cannot be negative"},
        {"{\"Type\":\"n3fjp\",\"PollMilliseconds\":1.5}",
         "PollMilliseconds must be a whole number"},
        {"{\"Type\":\"n3fjp\",\"RadioNr\":2}", "RadioNr applies to n1mm only"},
        {"{\"Type\":\"n3fjp\",\"StationName\":\"X\"}",
         "StationName applies to n1mm only"},
        {"{\"Type\":\"n3fjp\",\"TimeoutSeconds\":-1}",
         "TimeoutSeconds cannot be negative"},
        {"{\"Type\":\"n3fjp\",\"Address\":\"nowhere\"}", NULL},
        {"{\"Type\":\"n3fjp\",\"FollowAntenna\":true}", NULL},
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        j = NULL;
        json_parse(str_from_cstr(bad[i].json), &j);
        int e = freq_parse_source(j, 1, &c, &err);
        cJSON_Delete(j);
        test_check_int(e, ERR_INVALID, bad[i].json, "ERR_INVALID", __FILE__,
                       __LINE__);
        if (bad[i].message != NULL) {
            test_check_mem(err.message, strlen(err.message), bad[i].message,
                           strlen(bad[i].message), bad[i].json, __FILE__,
                           __LINE__);
        }
    }
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_lines);
    RUN(test_source);
    RUN(test_unasked);
    RUN(test_not_running);
    RUN(test_one_off);
    RUN(test_settings);
    plat_net_fini();
    return test_summary();
}
