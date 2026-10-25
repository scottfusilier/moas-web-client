/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* moas-web-client -watch against a fake server. */

#include "core/common.h"

#include <string.h>

#include "app/watch.h"
#include "core/buf.h"
#include "fake_moas_server.h"
#include "oasp_samples.h"
#include "test.h"

static struct buf output;

static void
capture(const char *text, void *arg)
{
    (void)arg;
    buf_append_cstr(&output, text);
}

static bool
has(const char *text)
{
    return strstr(buf_cstr(&output), text) != NULL;
}

/* The text run_until() is waiting for. */
static const char *awaited;

static bool
has_awaited(void *arg)
{
    (void)arg;
    return has(awaited);
}

/* Runs the loop until the output contains text. */
static bool
await_output(struct loop *loop, const char *text)
{
    awaited = text;
    return run_until(loop, has_awaited, NULL, 5000);
}

#define CHECK_OUTPUT_HAS(text) test_check(has(text), text, __FILE__, __LINE__)

static void
send_everything(struct fake_server *server, void *arg)
{
    (void)arg;
    fake_server_send_cstr(
        server,
        SAMPLE_SERVER SAMPLE_ROTATORS SAMPLE_CONFIGURATION SAMPLE_STATE);
}

static int
make_watch(struct loop *loop, struct fake_server *server,
           struct app_watch **out)
{
    const struct app_watch_options options = {
        .server = *fake_server_addr(server),
        .radio = {.station = 1, .radio = 2},
        .rows = 6,
        .columns = 5,
        .quiet_ms = 100,
        .retry_min_ms = 50,
    };
    return app_watch_create(loop, &options, capture, NULL, out);
}

static void
test_shows_radio_and_changes_nothing(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct app_watch *watch;

    buf_init(&output, 1024 * 1024);
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    fake_server_on_connect(server, send_everything, NULL);
    CHECK_INT_EQ(make_watch(loop, server, &watch), 0);

    CHECK(await_output(loop, "== on connect"));
    CHECK_OUTPUT_HAS("connected to 127.0.0.1:");
    CHECK_OUTPUT_HAS("Server: MOAS Server (M1DST) v2.0.2, grid 6 rows x 5 "
                     "columns\n");
    CHECK_OUTPUT_HAS("Station 1 radio 2, 1800-2000 kHz\n");
    CHECK_OUTPUT_HAS("  TX: 160m Inverted L (60)   RX: Bev-NE (50)\n");
    CHECK_OUTPUT_HAS("    0   0   60  160m Inverted L    selected     "
                     "available   \n");
    CHECK_OUTPUT_HAS("    0   4   50  Bev-NE             unavailable  "
                     "selected    \n");
    CHECK_OUTPUT_HAS("    5   0    0  Swap               function, state 0\n");

    /* A TR change is shown on its own line, and in later views. */
    fake_server_send_cstr(server, SAMPLE_TR);
    CHECK(await_output(loop, "TR station 1 radio 2: 3 transmit"));
    fake_server_send_cstr(server, SAMPLE_STATE);
    CHECK(await_output(loop, "== update"));
    CHECK_OUTPUT_HAS("TR: 3 transmit\n");

    /* An unknown message is reported, not fatal. */
    fake_server_send_cstr(server, SAMPLE_SURPRISE);
    CHECK(await_output(loop, "unrecognised \"Surprise\" message: {\"X\":1}"));

    /*
     * Read-only: in all that time the server received exactly one
     * message, the Hello.
     */
    struct str got = fake_server_received(server);
    CHECK(got.len > 0);
    CHECK(got.len > 0 && got.ptr[0] == '{' && got.ptr[got.len - 1] == '}');
    CHECK(strstr(buf_cstr(&output), "Hello") == NULL);
    size_t messages = 0;
    for (size_t i = 0; i + 10 < got.len; i++) {
        if (memcmp(got.ptr + i, "\"Message\":", 10) == 0) {
            messages++;
        }
    }
    CHECK_INT_EQ(messages, 1);
    CHECK(memcmp(got.ptr, "{\"Message\":\"Hello\"", 18) == 0);

    app_watch_destroy(watch);
    fake_server_destroy(server);
    loop_destroy(loop);
    buf_fini(&output);
}

static void
send_server_only(struct fake_server *server, void *arg)
{
    (void)arg;
    fake_server_send_cstr(server, SAMPLE_SERVER SAMPLE_ROTATORS);
}

static void
test_no_configuration_is_explained(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct app_watch *watch;

    buf_init(&output, 1024 * 1024);
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    fake_server_on_connect(server, send_server_only, NULL);
    CHECK_INT_EQ(make_watch(loop, server, &watch), 0);

    CHECK(await_output(loop, "== on connect"));
    CHECK_OUTPUT_HAS("Station 1 radio 2: no configuration received.");

    /* The connection drops: said, and it reconnects. */
    fake_server_drop_client(server);
    CHECK(await_output(loop, "no connection to 127.0.0.1:"));
    CHECK_OUTPUT_HAS("connection closed; trying again in 0.0 s");

    app_watch_destroy(watch);
    fake_server_destroy(server);
    loop_destroy(loop);
    buf_fini(&output);
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_shows_radio_and_changes_nothing);
    RUN(test_no_configuration_is_explained);
    plat_net_fini();
    return test_summary();
}
