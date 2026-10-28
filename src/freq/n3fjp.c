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
#include "freq/n3fjp.h"
#include "freq/n3fjp_reply.h"

#define CONNECT_TIMEOUT_MS 3000
#define REPLY_TIMEOUT_MS 2000
#define RETRY_MS 2000
/*
 * N3FJP sends a change as soon as it sees one, and Amateur Contact Log
 * reads the rig every 2 seconds, so polling more often finds nothing new;
 * the poll is there in case a change is ever missed.
 */
#define DEFAULT_POLL_MS 2000
#define MIN_POLL_MS 1000
#define MAX_POLL_MS 60000

/*
 * Between changes a reading is renewed only by the poll, so it must count
 * for longer than a poll and its reply take; this much longer, by default
 * and at least.
 */
#define TIMEOUT_MARGIN_MS (REPLY_TIMEOUT_MS + 1000)

static const char query[] = "<CMD><READBMF></CMD>\r\n";

struct n3fjp_config {
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    uint32_t poll_ms;
    bool timeout_given; /* TimeoutSeconds was set, not left to follow poll */
};

enum conn_state {
    WAITING,    /* for the next attempt */
    CONNECTING, /* connect() in progress */
    ASKING,     /* READBMF sent; waiting for the reply */
    RESTING,    /* between readings */
};

/*
 * One connection to an N3FJP program, used both by a source (which polls
 * for ever) and by a test (which reads once).
 */
struct n3fjp_conn {
    struct loop *loop;
    struct plat_addr addr;
    char addr_text[PLAT_ADDR_TEXT_MAX];
    uint32_t poll_ms;
    enum conn_state state;
    plat_socket sock;
    struct loop_timer timer;
    char line[CAPS_N3FJP_LINE];
    size_t line_len;
    bool skipping; /* dropping the rest of a line that was too long */

    /* What happens with results: a source's feed, or a test's callback. */
    void (*reading)(struct n3fjp_conn *c, int khz);
    void (*failure)(struct n3fjp_conn *c, int err, const char *message);
};

struct n3fjp_source {
    struct freq_source base;
    struct n3fjp_conn conn;
    struct freq_feed feed;
};

struct freq_n3fjp_test {
    struct n3fjp_conn conn;
    freq_n3fjp_test_fn *done;
    void *arg;
};

static void on_io(struct loop *loop, plat_socket sock, int events, void *arg);

static void
close_socket(struct n3fjp_conn *c)
{
    if (c->sock != PLAT_INVALID_SOCKET) {
        loop_unwatch(c->loop, c->sock);
        plat_socket_close(c->sock);
        c->sock = PLAT_INVALID_SOCKET;
    }
}

/* Closes the connection and reports why.  The callback may free c. */
static void
fail(struct n3fjp_conn *c, int err, const char *what)
{
    /*
     * Room for the address and the longest description below; the tracker
     * shortens it to CAPS_FREQ_ERROR if it must.
     */
    char message[PLAT_ADDR_TEXT_MAX + 128];

    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
    c->state = WAITING;
    snprintf(message, sizeof(message), "N3FJP at %s: %s", c->addr_text, what);
    c->failure(c, err, message);
}

static void
start_connect(struct n3fjp_conn *c)
{
    int err = plat_tcp_connect(&c->addr, &c->sock);
    if (err == 0) {
        err = loop_watch(c->loop, c->sock, LOOP_WRITE, on_io, c);
    }
    if (err != 0) {
        fail(c, err, err_str(err));
        return;
    }
    c->line_len = 0;
    c->skipping = false;
    c->state = CONNECTING;
    loop_timer_start(c->loop, &c->timer, CONNECT_TIMEOUT_MS, 0);
}

static void
ask(struct n3fjp_conn *c)
{
    size_t sent = 0;
    int err = plat_send(c->sock, query, sizeof(query) - 1, &sent);
    if (err == 0 && sent != sizeof(query) - 1) {
        err = ERR_IO; /* a request this short always fits */
    }
    if (err != 0) {
        fail(c, err, err_str(err));
        return;
    }
    c->state = ASKING;
    loop_timer_start(c->loop, &c->timer, REPLY_TIMEOUT_MS, 0);
}

/*
 * Handles a reading, whether the reply to our request, the program telling
 * us of a change, or its copy of a reply to another program.  The callback
 * may free c.
 */
static void
got_reading(struct n3fjp_conn *c, int khz)
{
    if (c->state == ASKING) {
        loop_timer_stop(c->loop, &c->timer);
        c->state = RESTING;
        loop_timer_start(c->loop, &c->timer, c->poll_ms, 0);
    }
    c->reading(c, khz);
}

/*
 * Handles one complete line: 0 with *khz set for a reading, ERR_NOT_FOUND
 * for a line to ignore, or another error after failing (which may free c).
 */
static int
handle_line(struct n3fjp_conn *c, struct str line, int *khz)
{
    int err = n3fjp_parse_line(line, khz);
    if (err == 0 || err == ERR_NOT_FOUND) {
        return err;
    }
    if (err == ERR_RANGE) {
        fail(c, err, "no usable frequency");
        return err;
    }
    struct str t = str_trim(line);
    char shown[48];
    size_t n = t.len < 40 ? t.len : 40;
    str_copy_clean(str_make(t.ptr, n), shown, sizeof(shown));
    char what[96];
    snprintf(what, sizeof(what), "unexpected reply \"%s\"", shown);
    fail(c, err, what);
    return err;
}

static void
on_io(struct loop *loop, plat_socket sock, int events, void *arg)
{
    (void)loop;
    (void)sock;
    (void)events;
    struct n3fjp_conn *c = arg;

    if (c->state == CONNECTING) {
        int err = plat_socket_error(c->sock);
        if (err == 0) {
            err = loop_watch(c->loop, c->sock, LOOP_READ, on_io, c);
        }
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
    c->line_len += got;

    /*
     * Several lines may come at once; only the last reading among them
     * matters, and it is passed on after the others are dealt with.
     */
    size_t start = 0;
    bool have = false;
    int khz = 0;
    for (;;) {
        const char *nl = memchr(c->line + start, '\n', c->line_len - start);
        if (nl == NULL) {
            break;
        }
        size_t end = (size_t)(nl - c->line);
        if (c->skipping) {
            c->skipping = false;
        } else {
            int k = 0;
            err = handle_line(c, str_make(c->line + start, end - start), &k);
            if (err == 0) {
                khz = k;
                have = true;
            } else if (err != ERR_NOT_FOUND) {
                return;
            }
        }
        start = end + 1;
    }
    memmove(c->line, c->line + start, c->line_len - start);
    c->line_len -= start;
    if (c->line_len == sizeof(c->line)) {
        /*
         * A line this long is no reading, but may be another command meant
         * for some other program; drop it rather than fail.
         */
        c->skipping = true;
        c->line_len = 0;
    }
    if (have) {
        got_reading(c, khz);
    }
}

static void
on_timer(struct loop *loop, void *arg)
{
    (void)loop;
    struct n3fjp_conn *c = arg;

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
conn_init(struct n3fjp_conn *c, struct loop *loop,
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
conn_fini(struct n3fjp_conn *c)
{
    close_socket(c);
    loop_timer_stop(c->loop, &c->timer);
}

/* The source. */

static struct n3fjp_source *
source_of(struct n3fjp_conn *c)
{
    char *p = (char *)c - offsetof(struct n3fjp_source, conn);
    return (struct n3fjp_source *)(void *)p;
}

static void
source_reading(struct n3fjp_conn *c, int khz)
{
    freq_feed_report(source_of(c)->feed, khz);
}

static void
source_failure(struct n3fjp_conn *c, int err, const char *message)
{
    (void)err;
    loop_timer_start(c->loop, &c->timer, RETRY_MS, 0);
    freq_feed_fail(source_of(c)->feed, message);
}

static int
n3fjp_start(struct freq_env *env, const struct freq_source_config *config,
            struct freq_feed feed, struct freq_source **out)
{
    const struct n3fjp_config *cfg = config->data;
    struct n3fjp_source *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return ERR_NOMEM;
    }
    s->base.ops = &freq_n3fjp_ops;
    s->feed = feed;
    s->conn.reading = source_reading;
    s->conn.failure = source_failure;
    conn_init(&s->conn, freq_env_loop(env), &cfg->addr, cfg->poll_ms);
    *out = &s->base;
    return 0;
}

static void
n3fjp_stop(struct freq_source *base)
{
    struct n3fjp_source *s = (struct n3fjp_source *)(void *)base;
    conn_fini(&s->conn);
    free(s);
}

static int
n3fjp_parse(const cJSON *json, int moas_radio,
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

    struct n3fjp_config *cfg = calloc(1, sizeof(*cfg));
    if (cfg == NULL) {
        return ERR_NOMEM;
    }
    config->data = cfg;

    e = freq_parse_address(json, FREQ_N3FJP_DEFAULT_ADDRESS, &cfg->addr,
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

    e = freq_parse_timeout(json, 0, &config->timeout_ms, err);
    if (e != 0) {
        return e;
    }
    uint32_t least = cfg->poll_ms + TIMEOUT_MARGIN_MS;
    cfg->timeout_given = config->timeout_ms != 0;
    if (!cfg->timeout_given) {
        config->timeout_ms = least;
    } else if (config->timeout_ms < least) {
        return freq_field_error(err, "TimeoutSeconds",
                                "TimeoutSeconds must be at least %g when "
                                "PollMilliseconds is %u",
                                least / 1000.0, (unsigned)cfg->poll_ms);
    }
    snprintf(config->name, sizeof(config->name), "N3FJP %s", cfg->addr_text);
    return 0;
}

static void
n3fjp_free_data(void *data)
{
    free(data);
}

/*
 * TimeoutSeconds is written only if set, so that one left to follow the
 * poll still does after the poll is changed on the page.
 */
static int
n3fjp_write(const struct freq_source_config *config, cJSON *obj)
{
    const struct n3fjp_config *cfg = config->data;
    bool ok = cJSON_AddStringToObject(obj, "Address", cfg->addr_text) !=
                  NULL &&
              cJSON_AddNumberToObject(obj, "PollMilliseconds", cfg->poll_ms) !=
                  NULL;
    if (ok && cfg->timeout_given) {
        ok = cJSON_AddNumberToObject(obj, "TimeoutSeconds",
                                     config->timeout_ms / 1000.0) != NULL;
    }
    return ok ? 0 : ERR_NOMEM;
}

const struct freq_source_ops freq_n3fjp_ops = {
    .type = "n3fjp",
    .parse = n3fjp_parse,
    .free_data = n3fjp_free_data,
    .write = n3fjp_write,
    .start = n3fjp_start,
    .stop = n3fjp_stop,
    .env_fini = NULL,
};

/* The one-off test. */

static struct freq_n3fjp_test *
test_of(struct n3fjp_conn *c)
{
    return (struct freq_n3fjp_test *)(void *)c; /* conn comes first */
}

static void
test_reading(struct n3fjp_conn *c, int khz)
{
    struct freq_n3fjp_test *t = test_of(c);
    conn_fini(c);
    freq_n3fjp_test_fn *done = t->done;
    void *arg = t->arg;
    free(t);
    done(0, khz, "", arg);
}

static void
test_failure(struct n3fjp_conn *c, int err, const char *message)
{
    struct freq_n3fjp_test *t = test_of(c);
    char copy[CAPS_FREQ_ERROR];
    snprintf(copy, sizeof(copy), "%s", message);
    conn_fini(c);
    freq_n3fjp_test_fn *done = t->done;
    void *arg = t->arg;
    free(t);
    done(err, 0, copy, arg);
}

int
freq_n3fjp_test(struct loop *loop, const struct plat_addr *addr,
                freq_n3fjp_test_fn *done, void *arg,
                struct freq_n3fjp_test **out)
{
    struct freq_n3fjp_test *t = calloc(1, sizeof(*t));
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
freq_n3fjp_test_cancel(struct freq_n3fjp_test *t)
{
    if (t != NULL) {
        conn_fini(&t->conn);
        free(t);
    }
}
