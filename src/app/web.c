/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/page.h"
#include "app/settings_file.h"
#include "app/web.h"
#include "core/buf.h"
#include "core/json.h"
#include "core/log.h"
#include "core/sha256.h"
#include "freq/n3fjp.h"
#include "freq/rigctld.h"

/* The largest snapshot sent (a station with many radios and buttons). */
#define SNAPSHOT_MAX (512 * 1024)

/* Body limits. */
#define BODY_SMALL 4096
#define BODY_SETTINGS (64 * 1024)

/* Highest frequency accepted from the page, in kHz. */
#define MAX_KHZ 500000

#define LOCAL_ONLY_MESSAGE                                                    \
    "settings can only be changed on the computer moas-web-client runs on"

struct app_web {
    struct loop *loop;
    struct app_runner *runner;
    struct http_router *router;
    struct http_server *server;
    char csp[512];

    /* Open live-update streams, and a pass-later timer to send to them. */
    struct http_stream *streams[CAPS_HTTP_STREAMS];
    struct loop_timer broadcast;
};

/* Replies. */

static void
reply_json(struct http_reply *reply, int status, cJSON *json)
{
    struct buf b;
    buf_init(&b, SNAPSHOT_MAX);
    if (json == NULL || json_write(&b, json) != 0) {
        http_reply_error(reply, 500, "out of memory");
    } else {
        buf_append_cstr(&b, "\n");
        http_reply_send(reply, status, "application/json", b.data, b.len);
    }
    buf_fini(&b);
    cJSON_Delete(json);
}

/* A field error, as the page expects it: {"Field": ..., "Message": ...}. */
static void
reply_field_error(struct http_reply *reply, int status, const char *field,
                  const char *message)
{
    cJSON *json = cJSON_CreateObject();
    if (json != NULL) {
        cJSON_AddStringToObject(json, "Field", field);
        cJSON_AddStringToObject(json, "Message", message);
    }
    reply_json(reply, status, json);
}

/* What happened to a request passed on to the MOAS server. */
static void
reply_sent(struct http_reply *reply, int err)
{
    if (err == 0) {
        http_reply_send(reply, 204, NULL, NULL, 0);
    } else if (err == ERR_CLOSED) {
        http_reply_error(reply, 503, "not connected to the MOAS server");
    } else if (err == ERR_INVALID) {
        http_reply_error(reply, 400, "the server does not offer that here");
    } else if (err == ERR_AGAIN) {
        http_reply_error(reply, 409,
                         "the server has not said which antenna "
                         "transmit uses yet");
    } else {
        char message[128];
        snprintf(message, sizeof(message),
                 "could not send to the MOAS server: %s", err_str(err));
        http_reply_error(reply, 502, message);
    }
}

/* The page, and snapshots. */

static void
on_page(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    struct app_web *w = arg;
    http_reply_header(reply, "Content-Security-Policy", w->csp);
    http_reply_send(reply, 200, "text/html; charset=utf-8", app_page_html,
                    app_page_html_len);
}

static void
on_state(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    struct app_web *w = arg;
    struct buf b;
    buf_init(&b, SNAPSHOT_MAX);
    if (app_link_snapshot(app_runner_link(w->runner), &b) != 0) {
        http_reply_error(reply, 500, "the snapshot is too large");
    } else {
        buf_append_cstr(&b, "\n");
        http_reply_send(reply, 200, "application/json", b.data, b.len);
    }
    buf_fini(&b);
}

/* Sends the snapshot to every open page. */
static void
on_broadcast(struct loop *loop, void *arg)
{
    (void)loop;
    struct app_web *w = arg;
    struct buf b;
    buf_init(&b, SNAPSHOT_MAX);
    if (app_link_snapshot(app_runner_link(w->runner), &b) == 0) {
        for (size_t i = 0; i < CAPS_HTTP_STREAMS; i++) {
            if (w->streams[i] != NULL) {
                /* A page that cannot keep up is dropped by the server. */
                http_stream_send(w->streams[i], buf_str(&b));
            }
        }
    }
    buf_fini(&b);
}

void
app_web_changed(struct app_web *w)
{
    /* Many changes in one pass make one snapshot, sent on the next. */
    if (w != NULL && !loop_timer_active(&w->broadcast)) {
        loop_timer_start(w->loop, &w->broadcast, 0, 0);
    }
}

static void
on_stream_gone(void *arg)
{
    struct http_stream **slot = arg;
    *slot = NULL;
}

static void
on_events(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    struct app_web *w = arg;
    struct http_stream **slot = NULL;
    for (size_t i = 0; i < CAPS_HTTP_STREAMS && slot == NULL; i++) {
        if (w->streams[i] == NULL) {
            slot = &w->streams[i];
        }
    }
    if (slot == NULL) {
        http_reply_error(reply, 503, "too many live-update streams open");
        return;
    }
    http_reply_on_cancel(reply, on_stream_gone, slot);
    if (http_reply_stream(reply, slot) != 0) {
        *slot = NULL;
        return;
    }
    /* The first snapshot at once. */
    struct buf b;
    buf_init(&b, SNAPSHOT_MAX);
    if (app_link_snapshot(app_runner_link(w->runner), &b) == 0) {
        http_stream_send(*slot, buf_str(&b));
    }
    buf_fini(&b);
}

/* Requests for the MOAS server. */

/*
 * Reads a request body: JSON holding only the given names.  On a problem,
 * replies 400 and returns NULL.
 */
static cJSON *
read_body(struct http_reply *reply, const struct http_request *req,
          const char *const *allowed)
{
    cJSON *json = NULL;
    if (json_parse(req->body, &json) != 0 || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        http_reply_error(reply, 400,
                         "bad request: the body is not a JSON "
                         "object");
        return NULL;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, json)
    {
        bool known = false;
        for (size_t i = 0; allowed[i] != NULL && !known; i++) {
            known = item->string != NULL &&
                    strcmp(item->string, allowed[i]) == 0;
        }
        if (!known) {
            char shown[48];
            const char *key = item->string != NULL ? item->string : "";
            size_t n = strlen(key);
            str_copy_clean(str_make(key, n < 40 ? n : 40), shown,
                           sizeof(shown));
            char message[96];
            snprintf(message, sizeof(message),
                     "bad request: unknown field \"%s\"", shown);
            cJSON_Delete(json);
            http_reply_error(reply, 400, message);
            return NULL;
        }
    }
    return json;
}

/*
 * A whole number that must be there.  Missing and present-but-zero are told
 * apart, since 0 is a valid antenna and function ID.
 */
static bool
get_required(const cJSON *json, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (item == NULL || cJSON_IsNull(item)) {
        return false;
    }
    return json_get_int(json, key, -1000000000, 1000000000, out) == 0;
}

/* The radio the request is for, which this client must serve. */
static bool
get_radio(struct http_reply *reply, struct app_web *w, const cJSON *json,
          struct oasp_radio_id *id)
{
    if (!get_required(json, "station", &id->station) ||
        !get_required(json, "radio", &id->radio))
    {
        http_reply_error(reply, 400, "station and radio are required");
        return false;
    }
    if (!app_link_has_radio(app_runner_link(w->runner), *id)) {
        http_reply_error(reply, 400, "this client does not serve that radio");
        return false;
    }
    return true;
}

static void
on_frequency(struct http_reply *reply, const struct http_request *req,
             void *arg)
{
    static const char *const allowed[] = {"station", "radio", "frequency",
                                          NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    struct oasp_radio_id id;
    int khz = 0;
    if (get_radio(reply, w, json, &id)) {
        if (!get_required(json, "frequency", &khz) || khz < 1 || khz > MAX_KHZ)
        {
            http_reply_error(reply, 400,
                             "frequency must be in kHz, between 1 "
                             "and 500000");
        } else {
            reply_sent(reply,
                       app_link_set_frequency(app_runner_link(w->runner), id,
                                              khz, "manual"));
        }
    }
    cJSON_Delete(json);
}

static void
on_select(struct http_reply *reply, const struct http_request *req, void *arg)
{
    static const char *const allowed[] = {"station", "radio", "antenna",
                                          "tx",      "rx",    NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    struct oasp_radio_id id;
    int antenna = 0;
    bool tx = false;
    bool rx = false;
    if (get_radio(reply, w, json, &id)) {
        bool ok = get_required(json, "antenna", &antenna) &&
                  json_get_bool(json, "tx", &tx) == 0 &&
                  json_get_bool(json, "rx", &rx) == 0 && (tx || rx);
        if (!ok) {
            http_reply_error(reply, 400,
                             "antenna and at least one of tx and "
                             "rx are required");
        } else {
            reply_sent(reply, app_link_select(app_runner_link(w->runner), id,
                                              antenna, tx, rx));
        }
    }
    cJSON_Delete(json);
}

static void
on_function(struct http_reply *reply, const struct http_request *req,
            void *arg)
{
    static const char *const allowed[] = {"station", "radio", "function",
                                          NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    struct oasp_radio_id id;
    int function = 0;
    if (get_radio(reply, w, json, &id)) {
        if (!get_required(json, "function", &function)) {
            http_reply_error(reply, 400, "function is required");
        } else {
            reply_sent(reply, app_link_function(app_runner_link(w->runner), id,
                                                function));
        }
    }
    cJSON_Delete(json);
}

/* {station, radio, direction: "next" or "previous"}. */
static void
on_step(struct http_reply *reply, const struct http_request *req, void *arg)
{
    static const char *const allowed[] = {"station", "radio", "direction",
                                          NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    struct oasp_radio_id id;
    char direction[16] = "";
    if (get_radio(reply, w, json, &id)) {
        json_get_text(json, "direction", direction, sizeof(direction));
        bool next = strcmp(direction, "next") == 0;
        if (!next && strcmp(direction, "previous") != 0) {
            http_reply_error(reply, 400,
                             "direction must be \"next\" or "
                             "\"previous\"");
        } else {
            reply_sent(reply,
                       app_link_step(app_runner_link(w->runner), id, next));
        }
    }
    cJSON_Delete(json);
}

/* {station, radio}: receive on the transmit antenna. */
static void
on_rx_same_as_tx(struct http_reply *reply, const struct http_request *req,
                 void *arg)
{
    static const char *const allowed[] = {"station", "radio", NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    struct oasp_radio_id id;
    if (get_radio(reply, w, json, &id)) {
        reply_sent(reply,
                   app_link_rx_same_as_tx(app_runner_link(w->runner), id));
    }
    cJSON_Delete(json);
}

/* Settings (from this computer only). */

/*
 * The file read afresh, so that hand edits show; when there is no file, or
 * it cannot be used, the settings in effect.
 */
static void
on_get_settings(struct http_reply *reply, const struct http_request *req,
                void *arg)
{
    (void)req;
    struct app_web *w = arg;
    const char *path = app_runner_path(w->runner);

    struct app_settings shown;
    struct app_settings_file info;
    struct app_field_error problem;
    bool have_problem = false;

    app_settings_defaults(&shown);
    int e = app_settings_load(path, &shown, &info, &problem);
    if (e == ERR_NOT_FOUND) {
        app_settings_fini(&shown);
        app_settings_copy(&shown, app_runner_settings(w->runner));
    } else if (e != 0) {
        /* Shown: the settings in effect, and what is wrong with the file. */
        have_problem = true;
        app_settings_fini(&shown);
        app_settings_copy(&shown, app_runner_settings(w->runner));
        /* As much of the file as could be read. */
        app_settings_load_partial(path, &shown);
    }

    cJSON *view = cJSON_CreateObject();
    if (view != NULL) {
        cJSON_AddItemToObject(view, "settings", app_settings_to_json(&shown));
        cJSON_AddStringToObject(view, "version", info.version);
        cJSON_AddBoolToObject(view, "exists", info.exists);
        cJSON_AddStringToObject(view, "path", path);
        if (have_problem) {
            cJSON *p = cJSON_AddObjectToObject(view, "problem");
            if (p != NULL) {
                cJSON_AddStringToObject(p, "Field", problem.field);
                cJSON_AddStringToObject(p, "Message", problem.message);
            }
        } else {
            cJSON_AddNullToObject(view, "problem");
        }
        cJSON_AddBoolToObject(view, "comments", info.had_comments);
        cJSON *list = cJSON_AddArrayToObject(view, "overrides");
        const char *const *overrides;
        size_t n = app_runner_overrides(w->runner, &overrides);
        for (size_t i = 0; list != NULL && i < n; i++) {
            cJSON_AddItemToArray(list, cJSON_CreateString(overrides[i]));
        }
        cJSON_AddStringToObject(view, "listen", app_runner_listen(w->runner));
    }
    app_settings_fini(&shown);
    reply_json(reply, 200, view);
}

static void
on_put_settings(struct http_reply *reply, const struct http_request *req,
                void *arg)
{
    static const char *const allowed[] = {"settings", "version", NULL};
    struct app_web *w = arg;
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return;
    }
    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(json, "settings");
    char version[SHA256_HEX_SIZE] = "";
    if (!cJSON_IsObject(settings) ||
        json_get_text(json, "version", version, sizeof(version)) != 0)
    {
        cJSON_Delete(json);
        http_reply_error(reply, 400, "settings are required");
        return;
    }

    struct app_settings next;
    struct app_field_error err;
    app_settings_defaults(&next);
    if (app_settings_from_json(settings, &next, NULL, &err) != 0) {
        cJSON_Delete(json);
        app_settings_fini(&next);
        reply_field_error(reply, 400, err.field, err.message);
        return;
    }
    cJSON_Delete(json);

    bool conflict = false;
    bool restart = false;
    int e = app_runner_save(w->runner, &next, version, &conflict, &restart,
                            &err);
    app_settings_fini(&next);
    if (conflict) {
        reply_field_error(reply, 409, "", err.message);
        return;
    }
    if (e != 0) {
        char message[320];
        snprintf(message, sizeof(message), "could not save the settings: %s",
                 err.message);
        reply_field_error(reply, 500, "", message);
        return;
    }
    char saved[SHA256_HEX_SIZE];
    app_settings_version(app_runner_path(w->runner), saved);
    cJSON *answer = cJSON_CreateObject();
    if (answer != NULL) {
        cJSON_AddBoolToObject(answer, "restart", restart);
        cJSON_AddStringToObject(answer, "version", saved);
    }
    reply_json(reply, 200, answer);
}

static void
on_heard(struct http_reply *reply, const struct http_request *req, void *arg)
{
    (void)req;
    struct app_web *w = arg;
    reply_json(reply, 200, app_runner_heard(w->runner));
}

/*
 * A rigctld or N3FJP test in progress, tied to the reply that waits for it;
 * one of the two handles is set.
 */
struct pending_test {
    struct http_reply *reply;
    struct freq_rigctld_test *rigctld;
    struct freq_n3fjp_test *n3fjp;
};

static void
on_test_done(int err, int khz, const char *message, void *arg)
{
    struct pending_test *p = arg;
    struct http_reply *reply = p->reply;
    free(p);
    if (err != 0) {
        reply_field_error(reply, 502, "", message);
        return;
    }
    cJSON *answer = cJSON_CreateObject();
    if (answer != NULL) {
        cJSON_AddNumberToObject(answer, "kHz", khz);
    }
    reply_json(reply, 200, answer);
}

/* The page went away first: stop the test. */
static void
on_test_abandoned(void *arg)
{
    struct pending_test *p = arg;
    freq_rigctld_test_cancel(p->rigctld);
    freq_n3fjp_test_cancel(p->n3fjp);
    free(p);
}

/*
 * Reads a Test request, {address}, and makes its pending_test; NULL after
 * replying with the problem.  example is a valid address for the message.
 */
static struct pending_test *
start_test(struct http_reply *reply, const struct http_request *req,
           const char *example, struct plat_addr *addr)
{
    static const char *const allowed[] = {"address", NULL};
    cJSON *json = read_body(reply, req, allowed);
    if (json == NULL) {
        return NULL;
    }
    char text[PLAT_ADDR_TEXT_MAX] = "";
    json_get_text(json, "address", text, sizeof(text));
    cJSON_Delete(json);
    struct str a = str_trim(str_from_cstr(text));
    if (plat_addr_parse(a.ptr, a.len, addr) != 0) {
        char message[160];
        snprintf(message, sizeof(message),
                 "Address \"%.*s\" is not host:port with a numeric host, "
                 "for example %s",
                 (int)a.len, a.ptr, example);
        reply_field_error(reply, 400, "Address", message);
        return NULL;
    }
    struct pending_test *p = calloc(1, sizeof(*p));
    if (p == NULL) {
        http_reply_error(reply, 500, "out of memory");
        return NULL;
    }
    p->reply = reply;
    return p;
}

/* Waits for the test that has just started, or replies that it did not. */
static void
wait_for_test(struct http_reply *reply, struct pending_test *p, int err)
{
    if (err != 0) {
        free(p);
        http_reply_error(reply, 500, "cannot start the test");
        return;
    }
    http_reply_on_cancel(reply, on_test_abandoned, p);
}

static void
on_rigctld_test(struct http_reply *reply, const struct http_request *req,
                void *arg)
{
    struct app_web *w = arg;
    struct plat_addr addr;
    struct pending_test *p = start_test(reply, req,
                                        FREQ_RIGCTLD_DEFAULT_ADDRESS, &addr);
    if (p != NULL) {
        wait_for_test(
            reply, p,
            freq_rigctld_test(w->loop, &addr, on_test_done, p, &p->rigctld));
    }
}

static void
on_n3fjp_test(struct http_reply *reply, const struct http_request *req,
              void *arg)
{
    struct app_web *w = arg;
    struct plat_addr addr;
    struct pending_test *p = start_test(reply, req, FREQ_N3FJP_DEFAULT_ADDRESS,
                                        &addr);
    if (p != NULL) {
        wait_for_test(
            reply, p,
            freq_n3fjp_test(w->loop, &addr, on_test_done, p, &p->n3fjp));
    }
}

/*
 * The page's Content-Security-Policy: its own inline script and style, by
 * hash, and requests to this server; nothing else.  Even if text from the
 * network ever ended up in the page as markup, the browser would not run
 * it.
 */
static void
build_csp(struct app_web *w)
{
    const char *page = (const char *)app_page_html;
    char script[SHA256_BASE64_SIZE] = "";
    char style[SHA256_BASE64_SIZE] = "";
    const struct {
        const char *open;
        const char *close;
        char *out;
    } blocks[] = {{"<script>", "</script>", script},
                  {"<style>", "</style>", style}};

    for (size_t i = 0; i < ARRAY_LEN(blocks); i++) {
        const char *start = strstr(page, blocks[i].open);
        const char *end = start != NULL ? strstr(start, blocks[i].close)
                                        : NULL;
        if (start != NULL && end != NULL) {
            start += strlen(blocks[i].open);
            sha256_base64(start, (size_t)(end - start), blocks[i].out);
        }
    }
    snprintf(w->csp, sizeof(w->csp),
             "default-src 'none'; script-src 'sha256-%s'; style-src "
             "'sha256-%s'; connect-src 'self'; base-uri 'none'; form-action "
             "'none'; frame-ancestors 'none'",
             script, style);
}

int
app_web_create(struct loop *loop, struct app_runner *runner,
               const struct http_server_options *options, struct app_web **out)
{
    struct app_web *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        return ERR_NOMEM;
    }
    w->loop = loop;
    w->runner = runner;
    loop_timer_init(&w->broadcast, on_broadcast, w);
    build_csp(w);

    const struct http_route_options small = {.max_body = BODY_SMALL};
    const struct http_route_options local = {
        .max_body = BODY_SMALL,
        .local_only = true,
        .local_only_message = LOCAL_ONLY_MESSAGE,
    };
    const struct http_route_options local_settings = {
        .max_body = BODY_SETTINGS,
        .local_only = true,
        .local_only_message = LOCAL_ONLY_MESSAGE,
    };
    int err = http_router_create(&w->router);
    struct {
        const char *method;
        const char *path;
        const struct http_route_options *options;
        http_handler_fn *fn;
    } routes[] = {
        {"GET", "/", &small, on_page},
        {"GET", "/events", &small, on_events},
        {"GET", "/api/state", &small, on_state},
        {"POST", "/api/frequency", &small, on_frequency},
        {"POST", "/api/select", &small, on_select},
        {"POST", "/api/function", &small, on_function},
        {"POST", "/api/step", &small, on_step},
        {"POST", "/api/rx-same-as-tx", &small, on_rx_same_as_tx},
        {"GET", "/api/settings", &local, on_get_settings},
        {"POST", "/api/settings", &local_settings, on_put_settings},
        {"GET", "/api/n1mm-heard", &local, on_heard},
        {"POST", "/api/rigctld-test", &local, on_rigctld_test},
        {"POST", "/api/n3fjp-test", &local, on_n3fjp_test},
    };
    for (size_t i = 0; err == 0 && i < ARRAY_LEN(routes); i++) {
        err = http_router_add(w->router, routes[i].method, routes[i].path,
                              routes[i].options, routes[i].fn, w);
    }
    if (err == 0) {
        err = http_server_create(loop, options, w->router, &w->server);
    }
    if (err != 0) {
        http_router_destroy(w->router);
        free(w);
        return err;
    }
    *out = w;
    return 0;
}

void
app_web_destroy(struct app_web *w)
{
    if (w == NULL) {
        return;
    }
    loop_timer_stop(w->loop, &w->broadcast);
    http_server_destroy(w->server);
    http_router_destroy(w->router);
    free(w);
}

const struct plat_addr *
app_web_addr(const struct app_web *w)
{
    return http_server_addr(w->server);
}
