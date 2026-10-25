/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

#include "core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"
#include "core/str.h"
#include "freq/rigctld.h"
#include "freq/rigctld_reply.h"

#define CONNECT_TIMEOUT_MS 3000
#define REPLY_TIMEOUT_MS 2000
#define RETRY_MS 2000
#define DEFAULT_POLL_MS 250
#define MIN_POLL_MS 50
#define MAX_POLL_MS 60000
#define DEFAULT_TIMEOUT_MS 2000

struct rigctld_config {
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    uint32_t poll_ms;
};

enum conn_state {
    WAITING,    /* for the next attempt */
    CONNECTING, /* connect() in progress */
    ASKING,     /* "f" sent; waiting for the reply */
    RESTING,    /* between readings */
};

/*
 * One connection to rigctld, used both by a source (which polls for ever)
 * and by a test (which reads once).
 */
struct rigctld_conn {
    struct loop *loop;
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    uint32_t poll_ms;
    enum conn_state state;
    plat_socket sock;
    struct loop_timer timer;
    char line[CAPS_RIGCTLD_LINE];
    size_t line_len;

    /* What happens with results: a source's feed, or a test's callback. */
    void (*reading)(struct rigctld_conn *c, int khz);
    void (*failure)(struct rigctld_conn *c, int err, const char *message);
};

struct rigctld_source {
    struct freq_source base;
    struct rigctld_conn conn;
    struct freq_feed feed;
};

struct freq_rigctld_test {
    struct rigctld_conn conn;
    freq_rigctld_test_fn *done;
    void *arg;
};

static void on_io(struct loop *loop, plat_socket sock, int events, void *arg);

static void
close_socket(struct rigctld_conn *c)
{
    if (c->sock != PLAT_INVALID_SOCKET) {
        loop_unwatch(c->loop, c->sock);
        plat_socket_close(c->sock);
        c->sock = PLAT_INVALID_SOCKET;
    }
}

/* Closes the connection and reports why.  The callback may free c. */
static void
fail(struct rigctld_conn *c, int err, const char *what)
{
    /*
     * Room for the address and the longest description below; the tracker
     * shortens it to CAPS_FREQ_ERROR if it must.
     */
    char message[PLAT_ADDR_TEXT_MAX + 128];

    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
    c->state = WAITING;
    snprintf(message, sizeof(message), "rigctld at %s: %s", c->addr_text,
             what);
    c->failure(c, err, message);
}

static void
start_connect(struct rigctld_conn *c)
{
    int err = plat_tcp_connect(&c->addr, &c->sock);
    if (err == 0) {
        err = loop_watch(c->loop, c->sock, LOOP_WRITE, on_io, c);
    }
    if (err != 0) {
        fail(c, err, err_str(err));
        return;
    }
    c->state = CONNECTING;
    loop_timer_start(c->loop, &c->timer, CONNECT_TIMEOUT_MS, 0);
}

static void
ask(struct rigctld_conn *c)
{
    size_t sent = 0;
    int err = plat_send(c->sock, "f\n", 2, &sent);
    if (err == 0 && sent != 2) {
        err = ERR_IO; /* two bytes always fit unless something is wrong */
    }
    if (err == 0) {
        err = loop_watch(c->loop, c->sock, LOOP_READ, on_io, c);
    }
    if (err != 0) {
        fail(c, err, err_str(err));
        return;
    }
    c->line_len = 0;
    c->state = ASKING;
    loop_timer_start(c->loop, &c->timer, REPLY_TIMEOUT_MS, 0);
}

/* Handles a complete reply line.  The callbacks may free c. */
static void
handle_line(struct rigctld_conn *c)
{
    int khz = 0;
    int report = 0;
    int err = rigctld_parse_reply(str_make(c->line, c->line_len), &khz,
                                  &report);
    if (err == ERR_IO) {
        char what[64];
        snprintf(what, sizeof(what), "rigctld reports error %d", report);
        fail(c, err, what);
        return;
    }
    if (err != 0) {
        char shown[48];
        size_t n = c->line_len < 40 ? c->line_len : 40;
        str_copy_clean(str_make(c->line, n), shown, sizeof(shown));
        char what[96];
        snprintf(what, sizeof(what), "unexpected reply \"%s\"", shown);
        fail(c, err, what);
        return;
    }
    loop_timer_stop(c->loop, &c->timer);
    c->state = RESTING;
    loop_timer_start(c->loop, &c->timer, c->poll_ms, 0);
    c->reading(c, khz);
}

static void
on_io(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    (void)events;
    struct rigctld_conn *c = arg;

    if (c->state == CONNECTING) {
        int err = plat_socket_error(c->sock);
        if (err != 0) {
            fail(c, err, err_str(err));
            return;
        }
        ask(c);
        return;
    }

    size_t room = sizeof(c->line) - c->line_len;
    size_t got = 0;
    int err = plat_recv(c->sock, c->line + c->line_len, room, &got);
    if (err == ERR_AGAIN) {
        return;
    }
    if (err != 0) {
        fail(c, err, err_str(err));
        return;
    }
    if (c->state != ASKING) {
        fail(c, ERR_INVALID, "sent something without being asked");
        return;
    }
    const char *nl = memchr(c->line + c->line_len, '\n', got);
    c->line_len += got;
    if (nl == NULL) {
        if (c->line_len >= sizeof(c->line)) {
            fail(c, ERR_LIMIT, "reply too long");
        }
        return;
    }
    /* Anything after the newline is not expected, and is dropped. */
    c->line_len = (size_t)(nl - c->line);
    handle_line(c);
}

static void
on_timer(struct loop *loop, void *arg)
{
    (void)loop;
    struct rigctld_conn *c = arg;

    switch (c->state) {
    case WAITING:
        start_connect(c);
        break;
    case CONNECTING:
        fail(c, ERR_TIMEOUT, "timed out connecting");
        break;
    case ASKING:
        fail(c, ERR_TIMEOUT, "no reply");
        break;
    case RESTING:
        ask(c);
        break;
    }
}

static void
conn_init(struct rigctld_conn *c, struct loop *loop,
          const struct plat_addr *addr, uint32_t poll_ms)
{
    c->loop = loop;
    c->addr = *addr;
    plat_addr_format(addr, c->addr_text);
    c->poll_ms = poll_ms;
    c->sock = PLAT_INVALID_SOCKET;
    c->state = WAITING;
    loop_timer_init(&c->timer, on_timer, c);
    /* Connect on the loop's next pass, so no callback runs before then. */
    loop_timer_start(loop, &c->timer, 0, 0);
}

static void
conn_fini(struct rigctld_conn *c)
{
    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
}

/* The source. */

static struct rigctld_source *
source_of(struct rigctld_conn *c)
{
    return (struct rigctld_source *)(void *)((char *)c -
                                             offsetof(struct rigctld_source,
                                                      conn));
}

static void
source_reading(struct rigctld_conn *c, int khz)
{
    freq_feed_report(source_of(c)->feed, khz);
}

static void
source_failure(struct rigctld_conn *c, int err, const char *message)
{
    (void)err;
    loop_timer_start(c->loop, &c->timer, RETRY_MS, 0);
    freq_feed_fail(source_of(c)->feed, message);
}

static int
rigctld_start(struct freq_env *env, const struct freq_source_config *config,
              struct freq_feed feed, struct freq_source **out)
{
    const struct rigctld_config *cfg = config->data;
    struct rigctld_source *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return ERR_NOMEM;
    }
    s->base.ops = &freq_rigctld_ops;
    s->feed = feed;
    s->conn.reading = source_reading;
    s->conn.failure = source_failure;
    conn_init(&s->conn, freq_env_loop(env), &cfg->addr, cfg->poll_ms);
    *out = &s->base;
    return 0;
}

static void
rigctld_stop(struct freq_source *base)
{
    struct rigctld_source *s = (struct rigctld_source *)(void *)base;
    conn_fini(&s->conn);
    free(s);
}

static int
rigctld_parse(const cJSON *json, int moas_radio,
              struct freq_source_config *config, struct freq_field_error *err)
{
    (void)moas_radio;
    static const char *const allowed[] = {"Address", "PollMilliseconds",
                                          "TimeoutSeconds", NULL};
    if (cJSON_GetObjectItemCaseSensitive(json, "RadioNr") != NULL) {
        return freq_field_error(err, "RadioNr",
                                "RadioNr applies to n1mm only");
    }
    if (cJSON_GetObjectItemCaseSensitive(json, "StationName") != NULL) {
        return freq_field_error(err, "StationName",
                                "StationName applies to n1mm only");
    }
    int e = freq_check_keys(json, allowed, err);
    if (e != 0) {
        return e;
    }

    struct rigctld_config *cfg = calloc(1, sizeof(*cfg));
    if (cfg == NULL) {
        return ERR_NOMEM;
    }
    config->data = cfg;

    e = freq_parse_address(json, FREQ_RIGCTLD_DEFAULT_ADDRESS, &cfg->addr,
                           cfg->addr_text, sizeof(cfg->addr_text), err);
    if (e != 0) {
        return e;
    }
    int poll = 0;
    e = json_get_int(json, "PollMilliseconds", -1000000, 1000000, &poll);
    if (e != 0) {
        return freq_field_error(err, "PollMilliseconds",
                                "PollMilliseconds must be a whole number");
    }
    if (poll < 0) {
        return freq_field_error(err, "PollMilliseconds",
                                "PollMilliseconds cannot be negative");
    }
    if (poll == 0) {
        poll = DEFAULT_POLL_MS;
    }
    if (poll < MIN_POLL_MS || poll > MAX_POLL_MS) {
        return freq_field_error(err, "PollMilliseconds",
                                "PollMilliseconds must be at least %d (and "
                                "at most %d)",
                                MIN_POLL_MS, MAX_POLL_MS);
    }
    cfg->poll_ms = (uint32_t)poll;

    e = freq_parse_timeout(json, DEFAULT_TIMEOUT_MS, &config->timeout_ms, err);
    if (e != 0) {
        return e;
    }
    snprintf(config->name, sizeof(config->name), "rigctld %s", cfg->addr_text);
    return 0;
}

static void
rigctld_free_data(void *data)
{
    free(data);
}

static int
rigctld_write(const struct freq_source_config *config, cJSON *obj)
{
    const struct rigctld_config *cfg = config->data;
    bool ok = cJSON_AddStringToObject(obj, "Address", cfg->addr_text) !=
                  NULL &&
              cJSON_AddNumberToObject(obj, "TimeoutSeconds",
                                      config->timeout_ms / 1000.0) != NULL &&
              cJSON_AddNumberToObject(obj, "PollMilliseconds", cfg->poll_ms) !=
                  NULL;
    return ok ? 0 : ERR_NOMEM;
}

const struct freq_source_ops freq_rigctld_ops = {
    .type = "rigctld",
    .parse = rigctld_parse,
    .free_data = rigctld_free_data,
    .write = rigctld_write,
    .start = rigctld_start,
    .stop = rigctld_stop,
    .env_fini = NULL,
};

/* The one-off test. */

static struct freq_rigctld_test *
test_of(struct rigctld_conn *c)
{
    return (struct freq_rigctld_test *)(void *)c; /* conn comes first */
}

static void
test_reading(struct rigctld_conn *c, int khz)
{
    struct freq_rigctld_test *t = test_of(c);
    conn_fini(c);
    freq_rigctld_test_fn *done = t->done;
    void *arg = t->arg;
    free(t);
    done(0, khz, "", arg);
}

static void
test_failure(struct rigctld_conn *c, int err, const char *message)
{
    struct freq_rigctld_test *t = test_of(c);
    char copy[CAPS_FREQ_ERROR];
    snprintf(copy, sizeof(copy), "%s", message);
    conn_fini(c);
    freq_rigctld_test_fn *done = t->done;
    void *arg = t->arg;
    free(t);
    done(err, 0, copy, arg);
}

int
freq_rigctld_test(struct loop *loop, const struct plat_addr *addr,
                  freq_rigctld_test_fn *done, void *arg,
                  struct freq_rigctld_test **out)
{
    struct freq_rigctld_test *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return ERR_NOMEM;
    }
    t->done = done;
    t->arg = arg;
    t->conn.reading = test_reading;
    t->conn.failure = test_failure;
    conn_init(&t->conn, loop, addr, DEFAULT_POLL_MS);
    *out = t;
    return 0;
}

void
freq_rigctld_test_cancel(struct freq_rigctld_test *t)
{
    if (t != NULL) {
        conn_fini(&t->conn);
        free(t);
    }
}
