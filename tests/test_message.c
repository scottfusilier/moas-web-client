/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* Decoding the server's messages and encoding the client's. */

#include "core/common.h"

#include <string.h>

#include "core/buf.h"
#include "oasp/oasp.h"
#include "oasp_samples.h"
#include "test.h"

static int
decode(const char *text, struct oasp_message *msg)
{
    return oasp_decode(str_from_cstr(text), msg);
}

#define CHECK_TEXT(got, want) CHECK_MEM_EQ((got), strlen(got), (want))

static void
test_server_and_rotators(void)
{
    struct oasp_message m;

    CHECK_INT_EQ(decode(SAMPLE_SERVER, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_SERVER);
    CHECK_TEXT(m.name, "Server");
    CHECK_INT_EQ(m.sequence, 1);
    CHECK_TEXT(m.u.server.name, "MOAS Server (M1DST) v2.0.2");
    CHECK_INT_EQ(m.u.server.rows, 6);
    CHECK_INT_EQ(m.u.server.columns, 5);
    CHECK(m.u.server.txrx);
    oasp_message_free(&m);

    CHECK_INT_EQ(decode(SAMPLE_ROTATORS, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_ROTATORS);
    CHECK_INT_EQ(m.u.rotators.count, 0);
    oasp_message_free(&m);
}

static void
test_configuration(void)
{
    struct oasp_message m;

    CHECK_INT_EQ(decode(SAMPLE_CONFIGURATION, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_CONFIGURATION);
    const struct oasp_configuration *c = &m.u.configuration;
    CHECK_INT_EQ(c->station, 1);
    CHECK_INT_EQ(c->radio, 2);
    CHECK(c->lower_khz == 1800.0 && c->upper_khz == 2000.0);
    CHECK_INT_EQ(c->antenna_count, 2);
    CHECK_INT_EQ(c->function_count, 1);
    if (c->antenna_count == 2 && c->function_count == 1) {
        const struct oasp_antenna_button *bev = &c->antennas[1];
        CHECK_INT_EQ(bev->antenna, 50);
        CHECK_TEXT(bev->short_name, "Bev-NE");
        CHECK_TEXT(bev->long_name, "Northeast Beverage");
        CHECK_INT_EQ(bev->row, 0);
        CHECK_INT_EQ(bev->column, 4);
        CHECK_INT_EQ(bev->description_count, 1);
        CHECK(bev->descriptions[0].width_3db == 80.0);
        CHECK_TEXT(bev->descriptions[0].model, "Beverage");
        const struct oasp_function_button *f = &c->functions[0];
        CHECK_TEXT(f->short_name, "Swap");
        CHECK(f->momentary && !f->alternate);
        CHECK_INT_EQ(f->row, 5);
    }
    oasp_message_free(&m);
}

static void
test_state_and_tr(void)
{
    struct oasp_message m;

    CHECK_INT_EQ(decode(SAMPLE_STATE, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_STATE);
    const struct oasp_state *s = &m.u.state;
    CHECK_INT_EQ(s->tx_id, 60);
    CHECK_INT_EQ(s->rx_id, 50);
    CHECK_TEXT(s->rx_short_name, "Bev-NE");
    CHECK_INT_EQ(s->antenna_count, 2);
    if (s->antenna_count == 2) {
        CHECK_INT_EQ(s->antennas[0].tx, OASP_SELECTED);
        CHECK_INT_EQ(s->antennas[0].rx, OASP_AVAILABLE);
        CHECK_INT_EQ(s->antennas[1].tx, OASP_UNAVAILABLE);
        CHECK_INT_EQ(s->antennas[1].rx, OASP_SELECTED);
    }
    oasp_message_free(&m);

    CHECK_INT_EQ(decode(SAMPLE_TR, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_TR);
    CHECK_INT_EQ(m.u.tr.tr, OASP_TR_TRANSMIT);
    CHECK_TEXT(oasp_tr_name(m.u.tr.tr), "transmit");
    CHECK_TEXT(oasp_tr_name(540), "");
    CHECK_TEXT(oasp_availability_name(7), "unknown");
    oasp_message_free(&m);
}

static void
test_unknown_and_missing(void)
{
    struct oasp_message m;

    CHECK_INT_EQ(decode(SAMPLE_SURPRISE, &m), 0);
    CHECK_INT_EQ(m.kind, OASP_UNKNOWN);
    CHECK_TEXT(m.name, "Surprise");
    CHECK(m.u.unknown.parameters != NULL);
    if (m.u.unknown.parameters != NULL) {
        CHECK_TEXT(m.u.unknown.parameters, "{\"X\":1}");
    }
    oasp_message_free(&m);

    /* Missing parameters and fields: zeros; unknown fields: ignored. */
    CHECK_INT_EQ(decode("{\"Message\":\"Server\",\"Extra\":[1,{}]}", &m), 0);
    CHECK_INT_EQ(m.kind, OASP_SERVER);
    CHECK_TEXT(m.u.server.name, "");
    CHECK_INT_EQ(m.u.server.rows, 0);
    oasp_message_free(&m);

    CHECK_INT_EQ(decode("{\"Message\":\"State\",\"Parameters\":"
                        "{\"Antennas\":null,\"RXId\":null}}",
                        &m),
                 0);
    CHECK_INT_EQ(m.u.state.antenna_count, 0);
    CHECK(m.u.state.antennas == NULL);
    oasp_message_free(&m);
}

static void
test_bad_messages(void)
{
    struct oasp_message m;

    /* Not messages at all: no name. */
    const char *not_messages[] = {
        "not json",
        "[1,2]",
        "{}",
        "{\"Message\":5}",
        "{\"Message\":\"\"}",
        "{\"Message\":\"TR\",\"Parameters\":[]}",
        "{\"Message\":\"TR\",\"Sequence\":-1}",
        "{\"Message\":\"TR\"} trailing",
    };
    for (size_t i = 0; i < ARRAY_LEN(not_messages); i++) {
        int err = decode(not_messages[i], &m);
        test_check_int(err, ERR_INVALID, not_messages[i], "ERR_INVALID",
                       __FILE__, __LINE__);
        CHECK(m.name[0] == '\0');
    }

    /* Bad parameters: the name says which message it was. */
    CHECK_INT_EQ(decode(SAMPLE_BAD_TR, &m), ERR_INVALID);
    CHECK_TEXT(m.name, "TR");
    CHECK_INT_EQ(m.kind, OASP_NONE);
    CHECK_INT_EQ(
        decode("{\"Message\":\"TR\",\"Parameters\":{\"TR\":1.5}}", &m),
        ERR_INVALID);
    CHECK_INT_EQ(
        decode("{\"Message\":\"TR\",\"Parameters\":{\"TR\":1e20}}", &m),
        ERR_RANGE);
    CHECK_INT_EQ(
        decode("{\"Message\":\"Server\",\"Parameters\":{\"TXRX\":1}}", &m),
        ERR_INVALID);
    /* An antenna that is not an object, after a good one: freed cleanly. */
    CHECK_INT_EQ(decode("{\"Message\":\"Configuration\",\"Parameters\":"
                        "{\"Antennas\":[{\"Antenna\":1},7]}}",
                        &m),
                 ERR_INVALID);
    CHECK_TEXT(m.name, "Configuration");
}

static void
test_limits_and_cleaning(void)
{
    struct oasp_message m;
    struct buf b;

    /* More antennas than CAPS_OASP_ANTENNAS. */
    buf_init(&b, 1024 * 1024);
    buf_append_cstr(&b,
                    "{\"Message\":\"State\",\"Parameters\":{\"Antennas\":[");
    for (int i = 0; i <= CAPS_OASP_ANTENNAS; i++) {
        buf_appendf(&b, "%s{\"Antenna\":%d}", i > 0 ? "," : "", i);
    }
    buf_append_cstr(&b, "]}}");
    CHECK_INT_EQ(oasp_decode(buf_str(&b), &m), ERR_LIMIT);
    buf_fini(&b);

    /* A name too long for CAPS_OASP_TEXT. */
    buf_init(&b, 1024 * 1024);
    buf_append_cstr(&b, "{\"Message\":\"Server\",\"Parameters\":{\"Name\":\"");
    for (int i = 0; i < CAPS_OASP_TEXT; i++) {
        buf_append_cstr(&b, "x");
    }
    buf_append_cstr(&b, "\"}}");
    CHECK_INT_EQ(oasp_decode(buf_str(&b), &m), ERR_LIMIT);
    buf_fini(&b);

    /* Control characters and escape codes in names become '?'. */
    CHECK_INT_EQ(decode("{\"Message\":\"Server\",\"Parameters\":"
                        "{\"Name\":\"A\\u001b[2JB\\nC\\u009bD r\\u00e9\"}}",
                        &m),
                 0);
    CHECK_TEXT(m.u.server.name, "A?[2JB?C?D r\xc3\xa9");
    oasp_message_free(&m);
}

static void
test_take(void)
{
    struct oasp_message a;
    struct oasp_message b;

    CHECK_INT_EQ(decode(SAMPLE_STATE, &a), 0);
    CHECK_INT_EQ(decode(SAMPLE_CONFIGURATION, &b), 0);
    /* b's old contents are freed; a is left empty. */
    oasp_message_take(&b, &a);
    CHECK_INT_EQ(b.kind, OASP_STATE);
    CHECK_INT_EQ(a.kind, OASP_NONE);
    CHECK(a.u.state.antennas == NULL);
    oasp_message_free(&a); /* freeing an empty message is harmless */
    oasp_message_free(&b);
}

static void
test_encode(void)
{
    struct buf b;
    const struct oasp_radio_id radio = {.station = 1, .radio = 2};
    const struct oasp_hello hello = {
        .name = "test",
        .bidirectional = true,
        .indicate_tr = true,
        .radios = &radio,
        .radio_count = 1,
        .rows = 6,
        .columns = 5,
    };

    buf_init(&b, 4096);
    CHECK_INT_EQ(oasp_encode_hello(&b, 1, &hello), 0);
    CHECK_MEM_EQ(b.data, b.len,
                 "{\"Message\":\"Hello\",\"Sequence\":1,\"Version\":1,"
                 "\"Parameters\":{\"Protocol\":\"OASP\",\"Name\":\"test\","
                 "\"Bidirectional\":true,\"IndicateTR\":true,\"Radios\":"
                 "[{\"Station\":1,\"Radio\":2}],\"Rows\":6,\"Columns\":5}}");

    /* IndicateTR is left out when false. */
    struct oasp_hello quiet = hello;
    quiet.indicate_tr = false;
    buf_clear(&b);
    CHECK_INT_EQ(oasp_encode_hello(&b, 1, &quiet), 0);
    CHECK(strstr(buf_cstr(&b), "IndicateTR") == NULL);

    buf_clear(&b);
    CHECK_INT_EQ(oasp_encode_radio(&b, 2, radio, 14000), 0);
    CHECK_INT_EQ(oasp_encode_antenna(&b, 3, radio, 20, true, true), 0);
    /* Antenna ID 0 is valid and must still be sent. */
    CHECK_INT_EQ(oasp_encode_antenna(&b, 4, radio, 0, false, true), 0);
    CHECK_INT_EQ(oasp_encode_function(&b, 5, radio, 1), 0);
    CHECK_MEM_EQ(
        b.data, b.len,
        "{\"Message\":\"Radio\",\"Sequence\":2,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Frequency\":14000}}"
        "{\"Message\":\"Antenna\",\"Sequence\":3,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Antennas\":[{\"TX\":1,\"Antenna\":20},"
        "{\"RX\":1,\"Antenna\":20}]}}"
        "{\"Message\":\"Antenna\",\"Sequence\":4,\"Version\":1,\"Parameters\":"
        "{\"Station\":1,\"Radio\":2,\"Antennas\":[{\"RX\":1,\"Antenna\":0}]}}"
        "{\"Message\":\"Function\",\"Sequence\":5,\"Version\":1,"
        "\"Parameters\":{\"Station\":1,\"Radio\":2,\"Function\":1,"
        "\"State\":1}}");

    /* Neither TX nor RX: nothing is written. */
    size_t len = b.len;
    CHECK_INT_EQ(oasp_encode_antenna(&b, 6, radio, 20, false, false),
                 ERR_INVALID);
    CHECK_INT_EQ(b.len, len);

    /* Too many radios for Hello. */
    struct oasp_hello crowded = hello;
    crowded.radio_count = CAPS_OASP_RADIOS + 1;
    CHECK_INT_EQ(oasp_encode_hello(&b, 1, &crowded), ERR_INVALID);
    buf_fini(&b);
}

int
main(void)
{
    RUN(test_server_and_rotators);
    RUN(test_configuration);
    RUN(test_state_and_tr);
    RUN(test_unknown_and_missing);
    RUN(test_bad_messages);
    RUN(test_limits_and_cleaning);
    RUN(test_take);
    RUN(test_encode);
    return test_summary();
}
