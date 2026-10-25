/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * moas-web-client from end to end: the page's requests, through the runner and
 * the link, to a fake MOAS server, all on loopback ports chosen by the system.
 * The fake behaves enough like a MOAS server to drive the link: it answers
 * Hello, sends a Configuration and State when a frequency is reported, and
 * a new State when an antenna is selected.  Antenna 12 can transmit;
 * antenna 50 is receive only.
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/runner.h"
#include "app/settings_file.h"
#include "app/web.h"
#include "core/json.h"
#include "core/sha256.h"
#include "fake_moas_server.h"
#include "fake_rig.h"
#include "http_client.h"
#include "platform/clock.h"
#include "platform/fs.h"
#include "test.h"

#define WAIT_MS 3000
#define SETTINGS_FILE "test_web_settings.json"

struct fixture {
    struct loop *loop;
    struct fake_server *fake;
    struct app_runner *runner;
    struct app_web *web;
    struct plat_addr site;
    cJSON *received[64]; /* messages the fake has not yet handed over */
    size_t received_count;
    int sequence;
    bool keyed;
};

static struct fixture fx;

static cJSON *
parse(const char *text)
{
    cJSON *json = NULL;
    json_parse(str_from_cstr(text), &json);
    return json;
}

/* A whole number in obj, or -1 if there is none. */
static int
number(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (int)item->valuedouble : -1;
}

static const char *
text(const cJSON *obj, const char *key)
{
    const char *s = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(obj, key));
    return s != NULL ? s : "";
}

static bool
is_true(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

/* The fake MOAS server. */

static void
fake_send(const char *name, const char *params)
{
    char *message = malloc(strlen(params) + 128);
    if (message == NULL) {
        return;
    }
    snprintf(message, strlen(params) + 128,
             "{\"Message\":\"%s\",\"Sequence\":%d,\"Version\":1,"
             "\"Parameters\":%s}",
             name, ++fx.sequence, params);
    fake_server_send_cstr(fx.fake, message);
    free(message);
}

static int
mark(int id, int selected, int available)
{
    return id == selected ? 2 : available;
}

static const char *
antenna_name(int id)
{
    return id == 12 ? "40m-Yagi" : id == 50 ? "Bev-NE" : "";
}

static void
send_state(int station, int radio, int tx, int rx)
{
    char p[512];
    snprintf(p, sizeof(p),
             "{\"Station\":%d,\"Radio\":%d,\"TXShortName\":\"%s\","
             "\"TXId\":%d,\"RXShortName\":\"%s\",\"RXId\":%d,"
             "\"Antennas\":[{\"Antenna\":12,\"TX\":%d,\"RX\":%d},"
             "{\"Antenna\":50,\"TX\":0,\"RX\":%d}],"
             "\"Functions\":[{\"Function\":0,\"State\":0}]}",
             station, radio, antenna_name(tx), tx, antenna_name(rx), rx,
             mark(12, tx, 1), mark(12, rx, 1), mark(50, rx, 1));
    fake_send("State", p);
}

static void
send_tr(int station, int radio, int tr)
{
    char p[96];
    snprintf(p, sizeof(p), "{\"Station\":%d,\"Radio\":%d,\"TR\":%d}", station,
             radio, tr);
    fake_send("TR", p);
}

static void
on_fake_connect(struct fake_server *server, void *arg)
{
    (void)server;
    (void)arg;
    fx.sequence = 0;
}

static void
on_fake_message(struct fake_server *server, const cJSON *msg, void *arg)
{
    (void)server;
    (void)arg;
    if (fx.received_count < ARRAY_LEN(fx.received)) {
        fx.received[fx.received_count++] = cJSON_Duplicate(msg, true);
    }
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "Parameters");
    const char *name = text(msg, "Message");
    int station = number(params, "Station");
    int radio = number(params, "Radio");

    if (strcmp(name, "Hello") == 0) {
        fake_send("Server", "{\"Name\":\"fake MOAS\",\"Rows\":6,"
                            "\"Columns\":5,\"TXRX\":true}");
        fake_send("Rotators", "{\"Rotators\":[]}");
    } else if (strcmp(name, "Radio") == 0) {
        char p[768];
        snprintf(p, sizeof(p),
                 "{\"Station\":%d,\"Radio\":%d,\"LowerFrequency\":7000,"
                 "\"UpperFrequency\":7300,\"Antennas\":["
                 "{\"Antenna\":12,\"ShortName\":\"40m-Yagi\",\"LongName\":"
                 "\"40m Yagi\",\"Row\":0,\"Column\":0},"
                 "{\"Antenna\":50,\"ShortName\":\"Bev-NE\",\"LongName\":"
                 "\"Northeast Beverage\",\"Row\":0,\"Column\":4}],"
                 "\"Functions\":[{\"Function\":0,\"ShortName\":\"Swap\","
                 "\"LongName\":\"Swap antennas\",\"Momentary\":true,"
                 "\"Row\":5,\"Column\":0}]}",
                 station, radio);
        fake_send("Configuration", p);
        send_state(station, radio, 12, 12);
        send_tr(station, radio, 2);
    } else if (strcmp(name, "Function") == 0) {
        /*
         * The fake has no switch to key, so Swap stands in for keying the
         * radio: it toggles between transmit and receive.
         */
        fx.keyed = !fx.keyed;
        send_tr(station, radio, fx.keyed ? 3 : 2);
    } else if (strcmp(name, "Antenna") == 0) {
        int tx = 12;
        int rx = 12;
        const cJSON *choice;
        cJSON_ArrayForEach(
            choice, cJSON_GetObjectItemCaseSensitive(params, "Antennas"))
        {
            if (cJSON_HasObjectItem(choice, "TX")) {
                tx = number(choice, "Antenna");
            }
            if (cJSON_HasObjectItem(choice, "RX")) {
                rx = number(choice, "Antenna");
            }
        }
        send_state(station, radio, tx, rx);
    }
}

/*
 * The next message the fake server received, which must be named name; its
 * Parameters, or NULL.  Free it with cJSON_Delete.
 */
static cJSON *
next_message(const char *name, int line)
{
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
    while (fx.received_count == 0 && plat_monotonic_ms() < end) {
        loop_run_once(fx.loop, 5);
    }
    if (fx.received_count == 0) {
        test_check(false, name, __FILE__, line);
        fprintf(stderr, "  the server did not receive a %s message\n", name);
        return NULL;
    }
    cJSON *msg = fx.received[0];
    fx.received_count--;
    memmove(&fx.received[0], &fx.received[1],
            fx.received_count * sizeof(fx.received[0]));
    if (strcmp(text(msg, "Message"), name) != 0) {
        test_check(false, name, __FILE__, line);
        fprintf(stderr, "  the server received %s, not %s\n",
                text(msg, "Message"), name);
    }
    cJSON *params = cJSON_DetachItemFromObjectCaseSensitive(msg, "Parameters");
    cJSON_Delete(msg);
    return params;
}
#define NEXT(name) next_message((name), __LINE__)

/* A Radio message for khz must be next. */
static void
expect_radio(int khz, int line)
{
    cJSON *p = next_message("Radio", line);
    test_check_int(number(p, "Frequency"), khz, "Frequency", "kHz", __FILE__,
                   line);
    cJSON_Delete(p);
}
#define EXPECT_RADIO(khz) expect_radio((khz), __LINE__)

/* Nothing may reach the server for a while. */
static void
expect_nothing(int ms, int line)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)ms;
    while (plat_monotonic_ms() < end) {
        loop_run_once(fx.loop, 5);
    }
    test_check(fx.received_count == 0, "nothing sent", __FILE__, line);
    while (fx.received_count > 0) {
        char *s = cJSON_PrintUnformatted(fx.received[--fx.received_count]);
        fprintf(stderr, "  the server received %s\n", s != NULL ? s : "?");
        cJSON_free(s);
        cJSON_Delete(fx.received[fx.received_count]);
    }
}
#define EXPECT_NOTHING(ms) expect_nothing((ms), __LINE__)

/* The page's view. */

static cJSON *
snapshot(void)
{
    struct buf b;
    cJSON *json = NULL;
    buf_init(&b, 1024 * 1024);
    if (app_link_snapshot(app_runner_link(fx.runner), &b) == 0) {
        json_parse(str_make(b.data, b.len), &json);
    }
    buf_fini(&b);
    return json;
}

static const cJSON *
radio_of(const cJSON *snap)
{
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(snap, "radios"),
                              0);
}

static int
button_count(const cJSON *snap)
{
    return cJSON_GetArraySize(
        cJSON_GetObjectItemCaseSensitive(radio_of(snap), "buttons"));
}

typedef bool condition_fn(const cJSON *snap);

static void
wait_for(condition_fn *ok, const char *what, int line)
{
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
    for (;;) {
        cJSON *snap = snapshot();
        bool done = snap != NULL && ok(snap);
        cJSON_Delete(snap);
        if (done) {
            return;
        }
        if (plat_monotonic_ms() >= end) {
            test_check(false, what, __FILE__, line);
            fprintf(stderr, "  timed out waiting for %s\n", what);
            return;
        }
        loop_run_once(fx.loop, 5);
    }
}
#define WAIT_FOR(ok) wait_for((ok), #ok, __LINE__)

static bool
server_named(const cJSON *snap)
{
    return strcmp(text(snap, "server"), "fake MOAS") == 0;
}

static bool
three_buttons(const cJSON *snap)
{
    return button_count(snap) == 3;
}

static bool
receiving(const cJSON *snap)
{
    return strcmp(text(radio_of(snap), "keying"), "receive") == 0;
}

static bool
transmitting(const cJSON *snap)
{
    return strcmp(text(radio_of(snap), "keying"), "transmit") == 0;
}

static bool
split(const cJSON *snap)
{
    const cJSON *r = radio_of(snap);
    return strcmp(text(r, "tx"), "40m-Yagi") == 0 &&
           strcmp(text(r, "rx"), "Bev-NE") == 0;
}

/* Nothing stale is shown while the connection is down. */
static bool
down_and_blank(const cJSON *snap)
{
    return !is_true(snap, "connected") && text(snap, "error")[0] != '\0' &&
           button_count(snap) == 0;
}

/* The fixture. */

static void
on_changed(void *arg)
{
    (void)arg;
    app_web_changed(fx.web);
}

/* The loop and the fake MOAS server. */
static bool
start_loop(void)
{
    memset(&fx, 0, sizeof(fx));
    plat_remove_file(SETTINGS_FILE);
    bool ok = loop_create(&fx.loop) == 0 &&
              fake_server_create(fx.loop, &fx.fake) == 0;
    CHECK(ok);
    if (ok) {
        fake_server_on_connect(fx.fake, on_fake_connect, NULL);
        fake_server_on_message(fx.fake, on_fake_message, NULL);
    }
    return ok;
}

/* Then moas-web-client itself; radio is the Radios entry (NULL: just radio 1).
 */
static bool
start_app(const char *radio)
{
    struct app_settings s;
    struct app_field_error err;
    bool radios = false;
    bool comments = false;
    char server[PLAT_ADDR_TEXT_MAX];
    char settings[1024];

    plat_addr_format(fake_server_addr(fx.fake), server);
    snprintf(settings, sizeof(settings),
             "{\"Server\":\"%s\",\"Columns\":4,\"Radios\":[%s]}", server,
             radio != NULL ? radio : "{\"Radio\":1}");

    app_settings_defaults(&s);
    int e = app_settings_parse(str_from_cstr(settings), &s, &radios, &comments,
                               &err);
    CHECK_INT_EQ(e, 0);
    const struct app_runner_options options = {
        .path = SETTINGS_FILE,
        .listen = "127.0.0.1:0",
        .retry_ms = 20,
    };
    struct http_server_options web = {0};
    plat_addr_parse("127.0.0.1:0", 11, &web.listen);
    if (e == 0) {
        e = app_runner_create(fx.loop, &options, &s, on_changed, NULL,
                              &fx.runner);
    }
    if (e == 0) {
        e = app_web_create(fx.loop, fx.runner, &web, &fx.web);
    }
    app_settings_fini(&s);
    CHECK_INT_EQ(e, 0);
    if (e != 0) {
        return false;
    }
    fx.site = *app_web_addr(fx.web);
    return true;
}

static bool
start(const char *radio)
{
    return start_loop() && start_app(radio);
}

static void
stop(void)
{
    app_web_destroy(fx.web);
    app_runner_destroy(fx.runner);
    fake_server_destroy(fx.fake);
    loop_destroy(fx.loop);
    while (fx.received_count > 0) {
        cJSON_Delete(fx.received[--fx.received_count]);
    }
    plat_remove_file(SETTINGS_FILE);
}

/* Sends a JSON body the way the page does; returns the status. */
static int
post(const char *path, const char *body)
{
    struct response r;
    int status = http_request(fx.loop, &fx.site, "POST", path, body, NULL, &r);
    response_fini(&r);
    return status;
}

/* The same, keeping the reply's body (parsed, or NULL). */
static int
request_json(const char *method, const char *path, const char *body,
             cJSON **out)
{
    struct response r;
    int status = http_request(fx.loop, &fx.site, method, path, body, NULL, &r);
    *out = r.body != NULL ? parse(r.body) : NULL;
    response_fini(&r);
    return status;
}

/* The connection is up and the server has answered Hello. */
static void
connect_up(void)
{
    cJSON_Delete(NEXT("Hello"));
    WAIT_FOR(server_named);
}

/* The tests. */

static void
test_page_drives_server(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();

    /* The grid size comes from the server, not from what was asked for. */
    cJSON *snap = snapshot();
    CHECK(is_true(snap, "connected"));
    CHECK_INT_EQ(number(snap, "rows"), 6);
    CHECK_INT_EQ(number(snap, "columns"), 5);
    cJSON_Delete(snap);

    CHECK_INT_EQ(post("/api/frequency",
                      "{\"station\":1,\"radio\":1,\"frequency\":7000}"),
                 204);
    EXPECT_RADIO(7000);
    WAIT_FOR(three_buttons);

    snap = snapshot();
    const cJSON *radio = radio_of(snap);
    const cJSON *buttons = cJSON_GetObjectItemCaseSensitive(radio, "buttons");
    const char *want[] = {
        "{\"row\":0,\"column\":0,\"kind\":\"antenna\",\"id\":12,\"name\":"
        "\"40m-Yagi\",\"longName\":\"40m Yagi\",\"tx\":2,\"rx\":2,"
        "\"state\":0}",
        "{\"row\":0,\"column\":4,\"kind\":\"antenna\",\"id\":50,\"name\":"
        "\"Bev-NE\",\"longName\":\"Northeast Beverage\",\"tx\":0,\"rx\":1,"
        "\"state\":0}",
        "{\"row\":5,\"column\":0,\"kind\":\"function\",\"id\":0,\"name\":"
        "\"Swap\",\"longName\":\"Swap antennas\",\"tx\":0,\"rx\":0,"
        "\"state\":0}",
    };
    for (int i = 0; i < 3; i++) {
        char *got = cJSON_PrintUnformatted(cJSON_GetArrayItem(buttons, i));
        CHECK(got != NULL);
        if (got != NULL) {
            CHECK_MEM_EQ(got, strlen(got), want[i]);
            cJSON_free(got);
        }
    }
    CHECK_INT_EQ(number(radio, "frequency"), 7000);
    CHECK_INT_EQ(number(radio, "lower"), 7000);
    CHECK_INT_EQ(number(radio, "upper"), 7300);
    CHECK_MEM_EQ(text(radio, "tx"), strlen(text(radio, "tx")), "40m-Yagi");
    CHECK_MEM_EQ(text(radio, "origin"), strlen(text(radio, "origin")),
                 "manual");
    cJSON_Delete(snap);
    WAIT_FOR(receiving);

    /* A receive-only selection gives a split. */
    CHECK_INT_EQ(post("/api/select", "{\"station\":1,\"radio\":1,"
                                     "\"antenna\":50,\"rx\":true}"),
                 204);
    cJSON *p = NEXT("Antenna");
    const cJSON *choices = cJSON_GetObjectItemCaseSensitive(p, "Antennas");
    CHECK_INT_EQ(cJSON_GetArraySize(choices), 1);
    const cJSON *choice = cJSON_GetArrayItem(choices, 0);
    CHECK_INT_EQ(number(choice, "Antenna"), 50);
    CHECK_INT_EQ(number(choice, "RX"), 1);
    CHECK(!cJSON_HasObjectItem(choice, "TX"));
    cJSON_Delete(p);
    WAIT_FOR(split);

    CHECK_INT_EQ(
        post("/api/function", "{\"station\":1,\"radio\":1,\"function\":0}"),
        204);
    p = NEXT("Function");
    CHECK_INT_EQ(number(p, "Function"), 0);
    CHECK_INT_EQ(number(p, "State"), 1);
    cJSON_Delete(p);
    WAIT_FOR(transmitting);
    stop();
}

static void
test_requests_refused(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();

    const char good[] =
        "{\"station\":1,\"radio\":1,\"antenna\":12,\"tx\":true,\"rx\":true}";
    char raw[512];
    struct response r;

    /* A form's content type, as another site could send. */
    snprintf(raw, sizeof(raw),
             "POST /api/select HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Content-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(good), good);
    CHECK_INT_EQ(http_raw(fx.loop, &fx.site, raw, true, &r), 415);
    response_fini(&r);

    /* Another site's origin. */
    CHECK_INT_EQ(http_request(fx.loop, &fx.site, "POST", "/api/select", good,
                              "Origin: http://evil.example\r\n", &r),
                 403);
    response_fini(&r);

    /* Another host name, as with DNS rebinding. */
    snprintf(raw, sizeof(raw),
             "POST /api/select HTTP/1.1\r\nHost: evil.example\r\n"
             "Content-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(good), good);
    CHECK_INT_EQ(http_raw(fx.loop, &fx.site, raw, true, &r), 403);
    response_fini(&r);

    const struct {
        const char *path;
        const char *body;
        const char *message;
    } cases[] = {
        {"/api/select",
         "{\"station\":1,\"radio\":2,\"antenna\":12,\"tx\":true}",
         "this client does not serve that radio"},
        {"/api/select", "{\"station\":1,\"radio\":1,\"tx\":true}",
         "antenna and at least one of tx and rx are required"},
        {"/api/select", "{\"station\":1,\"radio\":1,\"antenna\":12}",
         "antenna and at least one of tx and rx are required"},
        {"/api/select", "{\"radio\":1,\"antenna\":12,\"tx\":true}",
         "station and radio are required"},
        {"/api/function", "{\"station\":1,\"radio\":1}",
         "function is required"},
        {"/api/frequency", "{\"station\":1,\"radio\":1,\"frequency\":0}",
         "frequency must be in kHz, between 1 and 500000"},
        {"/api/frequency", "{\"station\":1,\"radio\":1,\"frequency\":500001}",
         "frequency must be in kHz, between 1 and 500000"},
        {"/api/frequency",
         "{\"station\":1,\"radio\":1,\"frequency\":7000,\"extra\":1}",
         "bad request: unknown field \"extra\""},
        {"/api/frequency", "frequency=7000", "bad request:"},
    };
    for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
        int status = http_request(fx.loop, &fx.site, "POST", cases[i].path,
                                  cases[i].body, NULL, &r);
        test_check_int(status, 400, cases[i].body, "400", __FILE__, __LINE__);
        test_check(r.body != NULL && strstr(r.body, cases[i].message) != NULL,
                   cases[i].message, __FILE__, __LINE__);
        response_fini(&r);
    }

    /* None of those may have reached the MOAS server. */
    EXPECT_NOTHING(100);
    stop();
}

static void
test_not_connected(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();
    fake_server_destroy(fx.fake);
    fx.fake = NULL;
    WAIT_FOR(down_and_blank);
    struct response r;
    CHECK_INT_EQ(http_request(fx.loop, &fx.site, "POST", "/api/select",
                              "{\"station\":1,\"radio\":1,\"antenna\":12,"
                              "\"tx\":true}",
                              NULL, &r),
                 503);
    CHECK(r.body != NULL &&
          strstr(r.body, "not connected to the MOAS server") != NULL);
    response_fini(&r);
    /* A typed frequency is kept for when the connection is back. */
    CHECK_INT_EQ(post("/api/frequency",
                      "{\"station\":1,\"radio\":1,\"frequency\":7000}"),
                 503);
    stop();
}

static void
test_reconnect_resends_frequency(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();
    post("/api/frequency", "{\"station\":1,\"radio\":1,\"frequency\":7000}");
    EXPECT_RADIO(7000);
    WAIT_FOR(three_buttons);

    fake_server_drop_client(fx.fake);
    WAIT_FOR(down_and_blank);

    cJSON_Delete(NEXT("Hello"));
    EXPECT_RADIO(7000);
    WAIT_FOR(three_buttons);
    stop();
}

/* The snapshot carried by the next event, or NULL. */
static cJSON *
read_event(struct client *c)
{
    struct buf event;
    cJSON *snap = NULL;
    buf_init(&event, 1024 * 1024);
    while (snap == NULL && client_event(c, &event)) {
        const char *data = buf_cstr(&event);
        if (strncmp(data, "data: ", 6) == 0) {
            snap = parse(data + 6);
            CHECK(snap != NULL);
            break;
        }
    }
    buf_fini(&event);
    return snap;
}

static void
test_event_stream(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();

    struct client c;
    struct response r;
    CHECK(client_open(&c, fx.loop, &fx.site));
    client_send(&c, "GET /events HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    CHECK(client_response(&c, &r, false));
    CHECK_INT_EQ(r.status, 200);
    CHECK(strstr(r.head, "Content-Type: text/event-stream\r\n") != NULL);
    response_fini(&r);

    cJSON *snap = read_event(&c);
    CHECK(snap != NULL && is_true(snap, "connected") && server_named(snap));
    cJSON_Delete(snap);

    post("/api/frequency", "{\"station\":1,\"radio\":1,\"frequency\":7000}");
    uint64_t end = plat_monotonic_ms() + WAIT_MS;
    bool arrived = false;
    while (!arrived && plat_monotonic_ms() < end) {
        snap = read_event(&c);
        if (snap == NULL) {
            break;
        }
        arrived = button_count(snap) == 3;
        cJSON_Delete(snap);
    }
    CHECK(arrived);
    client_close(&c);

    /* The page, with a policy that lets only its own script and style run. */
    CHECK_INT_EQ(http_request(fx.loop, &fx.site, "GET", "/", NULL, NULL, &r),
                 200);
    CHECK(strstr(r.head, "Content-Type: text/html; charset=utf-8\r\n") !=
          NULL);
    const char *script = r.body != NULL ? strstr(r.body, "<script>") : NULL;
    const char *script_end = script != NULL ? strstr(script, "</script>")
                                            : NULL;
    CHECK(script_end != NULL);
    if (script_end != NULL) {
        script += strlen("<script>");
        char hash[SHA256_BASE64_SIZE];
        char want[128];
        sha256_base64(script, (size_t)(script_end - script), hash);
        snprintf(want, sizeof(want), "script-src 'sha256-%s';", hash);
        CHECK(strstr(r.head, want) != NULL);
        CHECK(strstr(r.head, "default-src 'none';") != NULL);
        CHECK(strstr(r.head, "frame-ancestors 'none'") != NULL);
    }
    response_fini(&r);
    stop();
}

static void
send_n1mm(plat_socket tx, const struct plat_addr *to, long long freq,
          const char *connected)
{
    char packet[512];
    snprintf(packet, sizeof(packet),
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<RadioInfo>"
             "<app>N1MM</app><StationName>RUN-PC</StationName>"
             "<RadioNr>1</RadioNr><Freq>%lld</Freq><TXFreq>%lld</TXFreq>"
             "<IsConnected>%s</IsConnected></RadioInfo>",
             freq, freq, connected);
    plat_sendto(tx, packet, strlen(packet), to);
}

static void
wait_ms(int ms)
{
    uint64_t end = plat_monotonic_ms() + (uint64_t)ms;
    while (plat_monotonic_ms() < end) {
        loop_run_once(fx.loop, 5);
    }
}

/* Settings with N1MM first and rigctld second, both real sources. */
static void
test_follower(void)
{
    struct fake_rig rig;
    struct plat_addr n1mm;
    plat_socket probe = PLAT_INVALID_SOCKET;
    plat_socket tx = PLAT_INVALID_SOCKET;
    char n1mm_text[PLAT_ADDR_TEXT_MAX];
    char rig_text[PLAT_ADDR_TEXT_MAX];
    char radio[512];

    /* A free UDP port for N1MM's packets. */
    plat_addr_parse("127.0.0.1:0", 11, &n1mm);
    CHECK_INT_EQ(plat_udp_bind(&n1mm, &probe), 0);
    plat_socket_local_addr(probe, &n1mm);
    plat_socket_close(probe);
    plat_addr_format(&n1mm, n1mm_text);
    CHECK_INT_EQ(plat_udp_open(&n1mm, &tx), 0);

    if (!start_loop()) {
        plat_socket_close(tx);
        return;
    }
    fake_rig_start(&rig, fx.loop, "7010000");
    plat_addr_format(&rig.addr, rig_text);
    snprintf(radio, sizeof(radio),
             "{\"Radio\":1,\"Sources\":["
             "{\"Type\":\"n1mm\",\"Address\":\"%s\",\"RadioNr\":1},"
             "{\"Type\":\"rigctld\",\"Address\":\"%s\","
             "\"PollMilliseconds\":50}]}",
             n1mm_text, rig_text);
    if (!start_app(radio)) {
        fake_rig_stop(&rig);
        plat_socket_close(tx);
        stop();
        return;
    }
    connect_up();

    /* The lower-priority source alone is followed. */
    EXPECT_RADIO(7010);
    /* The higher-priority source arrives and takes over. */
    send_n1mm(tx, &n1mm, 1402500, "True");
    EXPECT_RADIO(14025);
    /* The lower one changing is not followed now. */
    snprintf(rig.reply, sizeof(rig.reply), "7015000");
    EXPECT_NOTHING(400);

    cJSON *snap = snapshot();
    const cJSON *r = radio_of(snap);
    const cJSON *sources = cJSON_GetObjectItemCaseSensitive(r, "sources");
    CHECK_MEM_EQ(text(r, "origin"), strlen(text(r, "origin")), "N1MM radio 1");
    CHECK_INT_EQ(cJSON_GetArraySize(sources), 2);
    const cJSON *first = cJSON_GetArrayItem(sources, 0);
    CHECK_MEM_EQ(text(first, "state"), strlen(text(first, "state")), "up");
    CHECK_INT_EQ(number(cJSON_GetArrayItem(sources, 1), "kHz"), 7015);
    cJSON_Delete(snap);

    /* N1MM's report shows on the settings page's list of what it heard. */
    cJSON *heard = NULL;
    CHECK_INT_EQ(request_json("GET", "/api/n1mm-heard", NULL, &heard), 200);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(heard, n1mm_text);
    const cJSON *one = cJSON_GetArrayItem(list, 0);
    CHECK_MEM_EQ(text(one, "stationName"), strlen(text(one, "stationName")),
                 "RUN-PC");
    CHECK_INT_EQ(number(one, "radioNr"), 1);
    CHECK_INT_EQ(number(one, "kHz"), 14025);
    cJSON_Delete(heard);

    /* A typed frequency holds until the followed source changes. */
    post("/api/frequency", "{\"station\":1,\"radio\":1,\"frequency\":21000}");
    EXPECT_RADIO(21000);
    snap = snapshot();
    r = radio_of(snap);
    CHECK_MEM_EQ(text(r, "origin"), strlen(text(r, "origin")), "manual");
    cJSON_Delete(snap);
    send_n1mm(tx, &n1mm, 1402500, "True");
    EXPECT_NOTHING(400);
    send_n1mm(tx, &n1mm, 1403000, "True");
    EXPECT_RADIO(14030);

    /* When the followed source fails, the next one takes over. */
    send_n1mm(tx, &n1mm, 1403000, "False");
    EXPECT_RADIO(7015);

    wait_ms(10);
    plat_socket_close(tx);
    fake_rig_stop(&rig);
    stop();
}

/* GET and POST /api/settings, as the settings section uses them. */
static void
test_settings_routes(void)
{
    if (!start(NULL)) {
        return;
    }
    connect_up();

    /* No file yet: the settings in effect. */
    cJSON *view = NULL;
    CHECK_INT_EQ(request_json("GET", "/api/settings", NULL, &view), 200);
    CHECK(!is_true(view, "exists"));
    CHECK_MEM_EQ(text(view, "version"), strlen(text(view, "version")), "");
    CHECK_MEM_EQ(text(view, "path"), strlen(text(view, "path")),
                 SETTINGS_FILE);
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(view, "problem")));
    CHECK_MEM_EQ(text(view, "listen"), strlen(text(view, "listen")),
                 "127.0.0.1:0");
    cJSON *settings = cJSON_DetachItemFromObjectCaseSensitive(view,
                                                              "settings");
    cJSON_Delete(view);
    CHECK_INT_EQ(number(settings, "Columns"), 4);

    /* Save a change, as the page does. */
    cJSON_ReplaceItemInObjectCaseSensitive(settings, "Station",
                                           cJSON_CreateNumber(2));
    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "settings", settings);
    cJSON_AddStringToObject(body, "version", "");
    char *text_body = cJSON_PrintUnformatted(body);
    cJSON *answer = NULL;
    CHECK_INT_EQ(request_json("POST", "/api/settings", text_body, &answer),
                 200);
    char version[SHA256_HEX_SIZE] = "";
    app_settings_version(SETTINGS_FILE, version);
    CHECK(version[0] != '\0');
    CHECK_MEM_EQ(text(answer, "version"), strlen(text(answer, "version")),
                 version);
    /* The page is served elsewhere than the saved Listen says. */
    CHECK(is_true(answer, "restart"));
    cJSON_Delete(answer);
    CHECK_INT_EQ(app_runner_settings(fx.runner)->station, 2);

    /* The same save again: the file is no longer the one it was based on. */
    CHECK_INT_EQ(request_json("POST", "/api/settings", text_body, &answer),
                 409);
    const char *conflict = "the settings file was changed by something else "
                           "since this form was opened";
    CHECK_MEM_EQ(text(answer, "Message"), strlen(text(answer, "Message")),
                 conflict);
    cJSON_Delete(answer);
    cJSON_free(text_body);

    /* A mistake is refused with the field it is in. */
    cJSON_ReplaceItemInObjectCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(body, "settings"), "Station",
        cJSON_CreateNumber(0));
    cJSON_ReplaceItemInObjectCaseSensitive(body, "version",
                                           cJSON_CreateString(version));
    text_body = cJSON_PrintUnformatted(body);
    CHECK_INT_EQ(request_json("POST", "/api/settings", text_body, &answer),
                 400);
    CHECK_MEM_EQ(text(answer, "Field"), strlen(text(answer, "Field")),
                 "Station");
    cJSON_Delete(answer);
    cJSON_free(text_body);
    cJSON_Delete(body);
    CHECK_INT_EQ(app_runner_settings(fx.runner)->station, 2);

    CHECK_INT_EQ(post("/api/settings", "{\"settings\":{},\"extra\":1}"), 400);
    CHECK_INT_EQ(post("/api/settings", "{\"version\":\"\"}"), 400);

    /* A hand edit with a mistake: shown, with the mistake marked. */
    const char edited[] = "{\"Server\": \"10.0.0.3:12059\", \"Station\": 0}";
    plat_replace_file(SETTINGS_FILE, edited, sizeof(edited) - 1);
    CHECK_INT_EQ(request_json("GET", "/api/settings", NULL, &view), 200);
    CHECK(is_true(view, "exists"));
    const cJSON *problem = cJSON_GetObjectItemCaseSensitive(view, "problem");
    CHECK_MEM_EQ(text(problem, "Field"), strlen(text(problem, "Field")),
                 "Station");
    const cJSON *shown = cJSON_GetObjectItemCaseSensitive(view, "settings");
    CHECK_MEM_EQ(text(shown, "Server"), strlen(text(shown, "Server")),
                 "10.0.0.3:12059");
    cJSON_Delete(view);

    /*
     * Settings may be larger than the other requests (4 KB), up to 64 KB.
     * The size is refused from the head alone, so only the head is sent.
     */
    struct response r;
    CHECK_INT_EQ(http_raw(fx.loop, &fx.site,
                          "POST /api/settings HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: 70000\r\n\r\n",
                          true, &r),
                 413);
    response_fini(&r);
    char *big = malloc(8001);
    CHECK(big != NULL);
    if (big != NULL) {
        memset(big, ' ', 8000);
        memcpy(big, "{\"version\":\"\"", 13);
        big[7999] = '}';
        big[8000] = '\0';
        CHECK_INT_EQ(post("/api/settings", big), 400); /* no settings */
        free(big);
    }
    CHECK_INT_EQ(http_raw(fx.loop, &fx.site,
                          "POST /api/frequency HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: 8000\r\n\r\n",
                          true, &r),
                 413);
    response_fini(&r);
    stop();
}

static void
test_rigctld_route(void)
{
    struct fake_rig rig;
    char body[160];
    char addr[PLAT_ADDR_TEXT_MAX];
    cJSON *answer = NULL;

    if (!start_loop()) {
        return;
    }
    fake_rig_start(&rig, fx.loop, "14025000");
    if (!start_app(NULL)) {
        fake_rig_stop(&rig);
        stop();
        return;
    }
    plat_addr_format(&rig.addr, addr);
    snprintf(body, sizeof(body), "{\"address\":\" %s \"}", addr);
    CHECK_INT_EQ(request_json("POST", "/api/rigctld-test", body, &answer),
                 200);
    CHECK_INT_EQ(number(answer, "kHz"), 14025);
    cJSON_Delete(answer);

    CHECK_INT_EQ(request_json("POST", "/api/rigctld-test",
                              "{\"address\":\"4532\"}", &answer),
                 400);
    CHECK_MEM_EQ(text(answer, "Field"), strlen(text(answer, "Field")),
                 "Address");
    cJSON_Delete(answer);

    /* rigctld answering with an error. */
    snprintf(rig.reply, sizeof(rig.reply), "RPRT -11");
    CHECK_INT_EQ(request_json("POST", "/api/rigctld-test", body, &answer),
                 502);
    CHECK(text(answer, "Message")[0] != '\0');
    cJSON_Delete(answer);

    fake_rig_stop(&rig);
    stop();
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_page_drives_server);
    RUN(test_requests_refused);
    RUN(test_not_connected);
    RUN(test_reconnect_resends_frequency);
    RUN(test_event_stream);
    RUN(test_follower);
    RUN(test_settings_routes);
    RUN(test_rigctld_route);
    plat_net_fini();
    return test_summary();
}
