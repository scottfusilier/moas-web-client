/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* rigctld's replies, the rigctld source, and the one-off Test. */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "freq/rigctld.h"
#include "freq/rigctld_reply.h"
#include "freq/source.h"
#include "fake_rig.h"
#include "platform/clock.h"
#include "test.h"

static void
test_replies(void)
{
    int khz = 0;
    int report = 0;

    CHECK_INT_EQ(rigctld_parse_reply(STR_LIT("14025000"), &khz, &report), 0);
    CHECK_INT_EQ(khz, 14025);
    CHECK_INT_EQ(
        rigctld_parse_reply(STR_LIT("7123456.000000\r"), &khz, &report), 0);
    CHECK_INT_EQ(khz, 7123);
    CHECK_INT_EQ(rigctld_parse_reply(STR_LIT("RPRT -9"), &khz, &report),
                 ERR_IO);
    CHECK_INT_EQ(report, -9);

    CHECK_INT_EQ(rigctld_parse_reply(STR_LIT("999"), &khz, &report),
                 ERR_RANGE);
    CHECK_INT_EQ(rigctld_parse_reply(STR_LIT("0"), &khz, &report), ERR_RANGE);
    CHECK_INT_EQ(rigctld_parse_reply(STR_LIT("2000000000000"), &khz, &report),
                 ERR_RANGE);
    const char *bad[] = {"",       "f",         "14.025e6",    "1.2.3", "RPRT",
                         "RPRT x", "-14025000", "14025000 Hz", ".5"};
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        int err = rigctld_parse_reply(str_from_cstr(bad[i]), &khz, &report);
        test_check(err == ERR_INVALID || err == ERR_RANGE, bad[i], __FILE__,
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
    snprintf(json, sizeof(json), "{\"Type\":\"rigctld\",\"Address\":\"%s\"%s}",
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

static void
test_source(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *t;
    struct freq_source *s;
    struct fake_rig rig;

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    fake_rig_start(&rig, loop, "14025000");
    CHECK_INT_EQ(
        start_source(env, &rig.addr, ",\"PollMilliseconds\":50", t, &s), 0);

    CHECK(run_until_state(loop, t, 14025, false, NULL, 3000));
    snprintf(rig.reply, sizeof(rig.reply), "7123456");
    CHECK(run_until_state(loop, t, 7123, false, NULL, 3000));

    /* The radio cannot be read: shown, then it reconnects and recovers. */
    snprintf(rig.reply, sizeof(rig.reply), "RPRT -9");
    CHECK(run_until_state(loop, t, 0, true, "rigctld reports error -9", 3000));
    snprintf(rig.reply, sizeof(rig.reply), "3525000");
    CHECK(run_until_state(loop, t, 3525, false, NULL, 5000));
    CHECK(rig.accepted >= 2);

    /* Nonsense, and no answer at all. */
    snprintf(rig.reply, sizeof(rig.reply), "hello\x1b[2J");
    CHECK(run_until_state(loop, t, 0, true, "unexpected reply \"hello?[2J\"",
                          3000));
    rig.silent = true;
    snprintf(rig.reply, sizeof(rig.reply), "3525000");
    CHECK(run_until_state(loop, t, 0, true, "no reply", 8000));

    freq_source_stop(s);
    fake_rig_stop(&rig);
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
    struct fake_rig rig;
    struct freq_rigctld_test *test;
    struct test_result r = {0};

    CHECK_INT_EQ(loop_create(&loop), 0);
    fake_rig_start(&rig, loop, "28074000");
    CHECK_INT_EQ(freq_rigctld_test(loop, &rig.addr, on_test_done, &r, &test),
                 0);
    for (int i = 0; i < 150 && !r.done; i++) {
        loop_run_once(loop, 20);
    }
    CHECK(r.done);
    CHECK_INT_EQ(r.err, 0);
    CHECK_INT_EQ(r.khz, 28074);

    /* A failing test reports why, once. */
    struct test_result r2 = {0};
    snprintf(rig.reply, sizeof(rig.reply), "RPRT -11");
    CHECK_INT_EQ(freq_rigctld_test(loop, &rig.addr, on_test_done, &r2, &test),
                 0);
    for (int i = 0; i < 150 && !r2.done; i++) {
        loop_run_once(loop, 20);
    }
    CHECK(r2.done && r2.err != 0);
    CHECK(strstr(r2.message, "rigctld reports error -11") != NULL);

    /* A cancelled test never calls back. */
    struct test_result r3 = {0};
    CHECK_INT_EQ(freq_rigctld_test(loop, &rig.addr, on_test_done, &r3, &test),
                 0);
    freq_rigctld_test_cancel(test);
    for (int i = 0; i < 20; i++) {
        loop_run_once(loop, 10);
    }
    CHECK(!r3.done);

    fake_rig_stop(&rig);
    loop_destroy(loop);
}

static void
test_settings(void)
{
    struct freq_source_config c;
    struct freq_field_error err;
    cJSON *j = NULL;

    json_parse(STR_LIT("{\"Type\":\"rigctld\"}"), &j);
    CHECK_INT_EQ(freq_parse_source(j, 3, &c, &err), 0);
    cJSON_Delete(j);
    CHECK_MEM_EQ(c.name, strlen(c.name), "rigctld 127.0.0.1:4532");
    CHECK_INT_EQ(c.timeout_ms, 2000);
    freq_source_config_fini(&c);

    struct {
        const char *json;
        const char *message;
    } bad[] = {
        {"{\"Type\":\"rigctld\",\"PollMilliseconds\":10}",
         "PollMilliseconds must be at least 50 (and at most 60000)"},
        {"{\"Type\":\"rigctld\",\"PollMilliseconds\":-1}",
         "PollMilliseconds cannot be negative"},
        {"{\"Type\":\"rigctld\",\"RadioNr\":2}",
         "RadioNr applies to n1mm only"},
        {"{\"Type\":\"rigctld\",\"StationName\":\"X\"}",
         "StationName applies to n1mm only"},
        {"{\"Type\":\"rigctld\",\"TimeoutSeconds\":-1}",
         "TimeoutSeconds cannot be negative"},
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        j = NULL;
        json_parse(str_from_cstr(bad[i].json), &j);
        int e = freq_parse_source(j, 1, &c, &err);
        cJSON_Delete(j);
        test_check_int(e, ERR_INVALID, bad[i].json, "ERR_INVALID", __FILE__,
                       __LINE__);
        test_check_mem(err.message, strlen(err.message), bad[i].message,
                       strlen(bad[i].message), bad[i].json, __FILE__,
                       __LINE__);
    }
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_replies);
    RUN(test_source);
    RUN(test_not_running);
    RUN(test_one_off);
    RUN(test_settings);
    plat_net_fini();
    return test_summary();
}
