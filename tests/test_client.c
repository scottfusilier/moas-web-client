/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* The MOAS client against a fake server, all on one event loop. */

#include "core/common.h"

#include <string.h>

#include "core/log.h"
#include "fake_moas_server.h"
#include "oasp/client.h"
#include "oasp_samples.h"
#include "test.h"

#define HELLO_TEST                                                            \
    "{\"Message\":\"Hello\",\"Sequence\":1,\"Version\":1,\"Parameters\":"     \
    "{\"Protocol\":\"OASP\",\"Name\":\"test\",\"Bidirectional\":true,"        \
    "\"IndicateTR\":true,\"Radios\":[{\"Station\":1,\"Radio\":2}],"           \
    "\"Rows\":6,\"Columns\":5}}"

static const struct oasp_radio_id radio = {.station = 1, .radio = 2};

/* What the callbacks saw. */
struct record {
    int connected;
    int disconnected;
    int last_error;
    uint32_t retries[8];
    int kinds[16];
    int messages;
    struct oasp_message kept; /* the Configuration, taken */
    int tr;
    struct oasp_client *destroy_on_message;
};

static void
on_connected(struct oasp_client *client, void *arg)
{
    (void)client;
    struct record *r = arg;
    r->connected++;
}

static void
on_message(struct oasp_client *client, struct oasp_message *msg, void *arg)
{
    (void)client;
    struct record *r = arg;
    if ((size_t)r->messages < ARRAY_LEN(r->kinds)) {
        r->kinds[r->messages] = (int)msg->kind;
    }
    r->messages++;
    if (msg->kind == OASP_CONFIGURATION) {
        oasp_message_take(&r->kept, msg);
    } else if (msg->kind == OASP_TR) {
        r->tr = msg->u.tr.tr;
    }
    if (r->destroy_on_message != NULL) {
        oasp_client_destroy(r->destroy_on_message);
        r->destroy_on_message = NULL;
    }
}

static void
on_disconnected(struct oasp_client *client, int err, uint32_t retry_ms,
                void *arg)
{
    (void)client;
    struct record *r = arg;
    if ((size_t)r->disconnected < ARRAY_LEN(r->retries)) {
        r->retries[r->disconnected] = retry_ms;
    }
    r->disconnected++;
    r->last_error = err;
}

static const struct oasp_client_events events = {
    .connected = on_connected,
    .message = on_message,
    .disconnected = on_disconnected,
};

static int
make_client(struct loop *loop, const struct plat_addr *addr, struct record *r,
            struct oasp_client **out)
{
    const struct oasp_client_options options = {
        .server = *addr,
        .hello =
            {
                .name = "test",
                .bidirectional = true,
                .indicate_tr = true,
                .radios = &radio,
                .radio_count = 1,
                .rows = 6,
                .columns = 5,
            },
        .connect_timeout_ms = 3000,
        .retry_min_ms = 50,
        .retry_max_ms = 400,
    };
    return oasp_client_create(loop, &options, &events, r, out);
}

/* Conditions for run_until(). */
struct wait {
    struct fake_server *server;
    struct record *record;
    size_t bytes;
    int count;
};

static bool
received_bytes(void *arg)
{
    struct wait *w = arg;
    return fake_server_received(w->server).len >= w->bytes;
}

static bool
got_messages(void *arg)
{
    struct wait *w = arg;
    return w->record->messages >= w->count;
}

static bool
got_disconnects(void *arg)
{
    struct wait *w = arg;
    return w->record->disconnected >= w->count;
}

static bool
got_accepts(void *arg)
{
    struct wait *w = arg;
    return fake_server_accepted(w->server) >= w->count &&
           fake_server_received(w->server).len >= strlen(HELLO_TEST);
}

static void
test_hello_and_sending(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct oasp_client *client;
    struct record r = {0};

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    CHECK_INT_EQ(make_client(loop, fake_server_addr(server), &r, &client), 0);
    /* Not connected until the loop runs. */
    CHECK(!oasp_client_is_connected(client));
    CHECK_INT_EQ(oasp_client_send_radio(client, radio, 14000), ERR_CLOSED);

    struct wait w = {.server = server, .bytes = strlen(HELLO_TEST)};
    CHECK(run_until(loop, received_bytes, &w, 5000));
    CHECK_INT_EQ(r.connected, 1);
    CHECK(oasp_client_is_connected(client));
    struct str got = fake_server_received(server);
    CHECK_MEM_EQ(got.ptr, got.len, HELLO_TEST);

    /* The messages, in the form the MOAS server expects. */
    CHECK_INT_EQ(oasp_client_send_radio(client, radio, 14000), 0);
    CHECK_INT_EQ(oasp_client_send_antenna(client, radio, 20, true, true), 0);
    CHECK_INT_EQ(oasp_client_send_antenna(client, radio, 0, false, true), 0);
    /* Refused, and does not use up a sequence number. */
    CHECK_INT_EQ(oasp_client_send_antenna(client, radio, 20, false, false),
                 ERR_INVALID);
    CHECK_INT_EQ(oasp_client_send_function(client, radio, 1), 0);
    const char rest[] =
        "{\"Message\":\"Radio\",\"Sequence\":2,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Frequency\":14000}}"
        "{\"Message\":\"Antenna\",\"Sequence\":3,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Antennas\":[{\"TX\":1,\"Antenna\":20},"
        "{\"RX\":1,\"Antenna\":20}]}}"
        "{\"Message\":\"Antenna\",\"Sequence\":4,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Antennas\":[{\"RX\":1,\"Antenna\":0}]}}"
        "{\"Message\":\"Function\",\"Sequence\":5,\"Version\":1,"
        "\"Parameters\":{\"Station\":1,\"Radio\":2,\"Function\":1,"
        "\"State\":1}}";
    w.bytes = strlen(HELLO_TEST) + sizeof(rest) - 1;
    CHECK(run_until(loop, received_bytes, &w, 5000));
    got = fake_server_received(server);
    CHECK(got.len == w.bytes);
    if (got.len == w.bytes) {
        CHECK_MEM_EQ(got.ptr + strlen(HELLO_TEST),
                     got.len - strlen(HELLO_TEST), rest);
    }

    oasp_client_destroy(client);
    fake_server_destroy(server);
    loop_destroy(loop);
}

/* Silences the expected warning about the bad message, and counts it. */
static int warnings;
static void
count_warnings(enum log_level level, const char *line, void *arg)
{
    (void)line;
    (void)arg;
    if (level == LOG_LEVEL_WARN) {
        warnings++;
    }
}

static void
send_stream(struct fake_server *server, void *arg)
{
    (void)arg;
    const char stream[] = SAMPLE_SERVER SAMPLE_ROTATORS SAMPLE_CONFIGURATION
        "\n" SAMPLE_STATE SAMPLE_TR SAMPLE_SURPRISE SAMPLE_BAD_TR SAMPLE_TR;
    /* In two parts, split inside the Configuration. */
    size_t half = strlen(SAMPLE_SERVER SAMPLE_ROTATORS) +
                  strlen(SAMPLE_CONFIGURATION) / 2;
    fake_server_send(server, stream, half);
    fake_server_send(server, stream + half, sizeof(stream) - 1 - half);
}

static void
test_receiving(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct oasp_client *client;
    struct record r = {0};

    warnings = 0;
    log_set_sink(count_warnings, NULL);
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    fake_server_on_connect(server, send_stream, NULL);
    CHECK_INT_EQ(make_client(loop, fake_server_addr(server), &r, &client), 0);

    /* Seven good messages; the bad TR is skipped, not fatal. */
    struct wait w = {.record = &r, .count = 7};
    CHECK(run_until(loop, got_messages, &w, 5000));
    run_until(loop, got_messages, &(struct wait){.record = &r, .count = 8},
              200);
    CHECK_INT_EQ(r.messages, 7);
    const int want[] = {OASP_SERVER, OASP_ROTATORS, OASP_CONFIGURATION,
                        OASP_STATE,  OASP_TR,       OASP_UNKNOWN,
                        OASP_TR};
    for (size_t i = 0; i < ARRAY_LEN(want); i++) {
        CHECK_INT_EQ(r.kinds[i], want[i]);
    }
    CHECK_INT_EQ(warnings, 1);
    CHECK_INT_EQ(r.disconnected, 0);
    /* A message taken by the callback stays valid afterwards. */
    CHECK_INT_EQ(r.kept.kind, OASP_CONFIGURATION);
    CHECK_INT_EQ(r.kept.u.configuration.antenna_count, 2);
    oasp_message_free(&r.kept);

    log_set_sink(NULL, NULL);
    oasp_client_destroy(client);
    fake_server_destroy(server);
    loop_destroy(loop);
}

static void
test_reconnect_after_drop(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct oasp_client *client;
    struct record r = {0};

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    CHECK_INT_EQ(make_client(loop, fake_server_addr(server), &r, &client), 0);
    struct wait w = {.server = server, .record = &r, .count = 1};
    CHECK(run_until(loop, got_accepts, &w, 5000));
    CHECK_INT_EQ(oasp_client_send_radio(client, radio, 7000), 0);

    fake_server_drop_client(server);
    CHECK(run_until(loop, got_disconnects, &w, 5000));
    CHECK_INT_EQ(r.last_error, ERR_CLOSED);
    CHECK_INT_EQ(r.retries[0], 50);
    CHECK(!oasp_client_is_connected(client));

    /* It comes back by itself, starting again with Hello as message 1. */
    w.count = 2;
    CHECK(run_until(loop, got_accepts, &w, 5000));
    struct str got = fake_server_received(server);
    CHECK_MEM_EQ(got.ptr, got.len, HELLO_TEST);
    CHECK_INT_EQ(r.connected, 2);

    oasp_client_destroy(client);
    fake_server_destroy(server);
    loop_destroy(loop);
}

static void
test_backoff_when_refused(void)
{
    struct loop *loop;
    struct oasp_client *client;
    struct record r = {0};
    struct plat_addr addr;
    plat_socket listener;

    /* A port with nothing listening. */
    CHECK_INT_EQ(plat_addr_parse("127.0.0.1:0", 11, &addr), 0);
    CHECK_INT_EQ(plat_tcp_listen(&addr, &listener), 0);
    CHECK_INT_EQ(plat_socket_local_addr(listener, &addr), 0);
    plat_socket_close(listener);

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(make_client(loop, &addr, &r, &client), 0);
    /* Windows takes about two seconds to report each refusal. */
    struct wait w = {.record = &r, .count = 3};
    CHECK(run_until(loop, got_disconnects, &w, 20000));
    CHECK_INT_EQ(r.last_error, ERR_REFUSED);
    CHECK_INT_EQ(r.connected, 0);
    /* The pause doubles each time. */
    CHECK_INT_EQ(r.retries[0], 50);
    CHECK_INT_EQ(r.retries[1], 100);
    CHECK_INT_EQ(r.retries[2], 200);

    oasp_client_destroy(client);
    loop_destroy(loop);
}

static void
send_junk(struct fake_server *server, void *arg)
{
    (void)arg;
    fake_server_send_cstr(server, SAMPLE_SERVER "this is not JSON");
}

static void
test_junk_drops_connection(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct oasp_client *client;
    struct record r = {0};

    log_set_sink(count_warnings, NULL);
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    fake_server_on_connect(server, send_junk, NULL);
    CHECK_INT_EQ(make_client(loop, fake_server_addr(server), &r, &client), 0);
    struct wait w = {.record = &r, .count = 1};
    CHECK(run_until(loop, got_disconnects, &w, 5000));
    CHECK_INT_EQ(r.messages, 1); /* the Server message before the junk */
    CHECK_INT_EQ(r.last_error, ERR_INVALID);

    log_set_sink(NULL, NULL);
    oasp_client_destroy(client);
    fake_server_destroy(server);
    loop_destroy(loop);
}

static void
test_destroy_inside_callback(void)
{
    struct loop *loop;
    struct fake_server *server;
    struct oasp_client *client;
    struct record r = {0};

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(fake_server_create(loop, &server), 0);
    fake_server_on_connect(server, send_stream, NULL);
    CHECK_INT_EQ(make_client(loop, fake_server_addr(server), &r, &client), 0);
    /*
     * Destroyed while handling the first of several messages that arrived
     * together: the rest are not delivered, and nothing touches freed
     * memory (the sanitizers would say).
     */
    r.destroy_on_message = client;
    struct wait w = {.record = &r, .count = 1};
    CHECK(run_until(loop, got_messages, &w, 5000));
    run_until(loop, got_messages, &(struct wait){.record = &r, .count = 2},
              200);
    CHECK_INT_EQ(r.messages, 1);

    fake_server_destroy(server);
    loop_destroy(loop);
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_hello_and_sending);
    RUN(test_receiving);
    RUN(test_reconnect_after_drop);
    RUN(test_backoff_when_refused);
    RUN(test_junk_drops_connection);
    RUN(test_destroy_inside_callback);
    plat_net_fini();
    return test_summary();
}
