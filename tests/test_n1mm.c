/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/* N1MM's RadioInfo packets, and the n1mm source listening for them. */

#include "core/common.h"

#include <stdio.h>
#include <string.h>

#include "core/json.h"
#include "freq/n1mm.h"
#include "freq/n1mm_xml.h"
#include "freq/source.h"
#include "platform/clock.h"
#include "test.h"

/* A packet as N1MM sends it. */
static void
make_packet(char *out, size_t size, const char *station, int radio_nr,
            long long freq, const char *connected)
{
    snprintf(out, size,
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
             "<RadioInfo>\r\n"
             "\t<app>N1MM</app>\r\n"
             "\t<StationName>%s</StationName>\r\n"
             "\t<RadioNr>%d</RadioNr>\r\n"
             "\t<Freq>%lld</Freq>\r\n"
             "\t<TXFreq>%lld</TXFreq>\r\n"
             "\t<Mode>CW</Mode>\r\n"
             "\t<OpCall>W1AW</OpCall>\r\n"
             "\t<IsRunning>False</IsRunning>\r\n"
             "\t<FocusEntry>0</FocusEntry>\r\n"
             "\t<Antenna>2</Antenna>\r\n"
             "\t<Rotors></Rotors>\r\n"
             "\t<IsConnected>%s</IsConnected>\r\n"
             "</RadioInfo>",
             station, radio_nr, freq, freq, connected);
}

/*
 * A packet as DXLog.net 2.6 sends it (captured, with a generic call): the
 * same layout, but <app>DXLog</app>, <Station> for the station name, and
 * no IsConnected.
 */
static void
make_dxlog_packet(char *out, size_t size, const char *station, int radio_nr,
                  long long freq)
{
    snprintf(out, size,
             "<?xml version=\"1.0\"?><RadioInfo><app>DXLog</app>"
             "<logger>DXLog.net v2.6.37</logger><Station>%s</Station>"
             "<RadioNr>%d</RadioNr><Freq>%lld</Freq><TXFreq>%lld</TXFreq>"
             "<InactiveFreq>355150</InactiveFreq><Mode>SSB</Mode>"
             "<OpCall></OpCall><mycall>W1AW</mycall>"
             "<IsRunning>False</IsRunning><FocusEntry>197552</FocusEntry>"
             "<Antenna>0</Antenna><Rotors></Rotors>"
             "<FocusRadioNr>1</FocusRadioNr><IsStereo>False</IsStereo>"
             "<IsSplit>False</IsSplit><ActiveRadioNr>1</ActiveRadioNr>"
             "<Technique>SO2R</Technique><StationType>R</StationType>"
             "<IsTransmitting>False</IsTransmitting><IFFrequency>"
             "</IFFrequency></RadioInfo>",
             station, radio_nr, freq, freq);
}

static int
parse(const char *text, struct n1mm_radio_info *info)
{
    return n1mm_parse_radio_info(str_from_cstr(text), info);
}

static void
test_parse_packets(void)
{
    char p[2048];
    struct n1mm_radio_info info;

    make_packet(p, sizeof(p), "RUN-PC", 2, 2129999, "True");
    CHECK_INT_EQ(parse(p, &info), 0);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "RUN-PC");
    CHECK_INT_EQ(info.radio_nr, 2);
    CHECK_INT_EQ(n1mm_khz(&info), 21299); /* rounded down */
    CHECK(info.connected);

    make_packet(p, sizeof(p), "  MULT &amp; RUN  ", 1, 700500, " false ");
    CHECK_INT_EQ(parse(p, &info), 0);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "MULT & RUN");
    CHECK(!info.connected);

    /* Minimal forms: no declaration, self-closing and empty elements. */
    CHECK_INT_EQ(parse("<RadioInfo><RadioNr>1</RadioNr><TXFreq/>"
                       "<Freq></Freq></RadioInfo>",
                       &info),
                 0);
    CHECK_INT_EQ(info.radio_nr, 1);
    CHECK_INT_EQ(info.freq, 0);
    CHECK_INT_EQ(parse("<RadioInfo/>", &info), 0);
    CHECK_INT_EQ(parse("\xef\xbb\xbf<RadioInfo a=\"x>y\"><Freq>1</Freq>"
                       "</RadioInfo >\n",
                       &info),
                 0);
    CHECK_INT_EQ(info.freq, 1);

    /* A station name with control characters is cleaned. */
    CHECK_INT_EQ(parse("<RadioInfo><StationName>A&#27;[2J\x1b"
                       "B</StationName></RadioInfo>",
                       &info),
                 0);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "A?[2J?B");
}

/* Which program sent a packet, and DXLog's name for the station. */
static void
test_parse_apps(void)
{
    char p[2048];
    struct n1mm_radio_info info;

    make_packet(p, sizeof(p), "RUN-PC", 1, 1402550, "True");
    CHECK_INT_EQ(parse(p, &info), 0);
    CHECK_INT_EQ(info.app, N1MM_APP_N1MM);

    make_dxlog_packet(p, sizeof(p), "STN1", 2, 2134500);
    CHECK_INT_EQ(parse(p, &info), 0);
    CHECK_INT_EQ(info.app, N1MM_APP_DXLOG);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "STN1");
    CHECK_INT_EQ(info.radio_nr, 2);
    CHECK_INT_EQ(n1mm_khz(&info), 21345);
    CHECK(info.connected); /* DXLog does not say otherwise */

    CHECK_INT_EQ(parse("<RadioInfo><Freq>1</Freq></RadioInfo>", &info), 0);
    CHECK_INT_EQ(info.app, N1MM_APP_UNKNOWN);
    CHECK_INT_EQ(parse("<RadioInfo><app> dxlog </app></RadioInfo>", &info), 0);
    CHECK_INT_EQ(info.app, N1MM_APP_DXLOG);
    CHECK_INT_EQ(parse("<RadioInfo><app>Other</app></RadioInfo>", &info), 0);
    CHECK_INT_EQ(info.app, N1MM_APP_OTHER);

    /* StationName wins over Station, in either order. */
    CHECK_INT_EQ(parse("<RadioInfo><Station>B</Station><StationName>A"
                       "</StationName></RadioInfo>",
                       &info),
                 0);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "A");
    CHECK_INT_EQ(parse("<RadioInfo><StationName>A</StationName><Station>B"
                       "</Station></RadioInfo>",
                       &info),
                 0);
    CHECK_MEM_EQ(info.station_name, strlen(info.station_name), "A");
}

static void
test_refuses_other_packets(void)
{
    struct n1mm_radio_info info;
    const char *bad[] = {
        "",
        "<contactinfo><call>W1AW</call></contactinfo>",
        "<RadioInfo>",
        "<RadioInfo><Freq>14025",
        "<RadioInfo><Freq>1</Mode></RadioInfo>",
        "<RadioInfo><Freq>abc</Freq></RadioInfo>",
        "<RadioInfo><Freq>-5</Freq></RadioInfo>",
        "<RadioInfo><RadioNr>1.5</RadioNr></RadioInfo>",
        "<RadioInfo><Freq>999999999999999</Freq></RadioInfo>",
        "<RadioInfo><A><B>1</B></A></RadioInfo>",
        "<RadioInfo><Freq>1</Freq></RadioInfo>trailing",
        "<RadioInfo><StationName>a&bogus;</StationName></RadioInfo>",
        "<RadioInfoX></RadioInfoX>",
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        int err = parse(bad[i], &info);
        test_check_int(err, ERR_INVALID, bad[i], "ERR_INVALID", __FILE__,
                       __LINE__);
    }
}

/* Parses one source's settings from JSON text. */
static int
parse_source(const char *text, int moas_radio,
             struct freq_source_config *config, struct freq_field_error *err)
{
    cJSON *json = NULL;
    if (json_parse(str_from_cstr(text), &json) != 0) {
        return -1;
    }
    int e = freq_parse_source(json, moas_radio, config, err);
    cJSON_Delete(json);
    return e;
}

static void
test_settings(void)
{
    struct freq_source_config c;
    struct freq_field_error err;

    /* Defaults: N1MM's address, the MOAS radio number, never stale. */
    CHECK_INT_EQ(parse_source("{\"Type\":\" N1MM \"}", 2, &c, &err), 0);
    CHECK_MEM_EQ(c.name, strlen(c.name), "N1MM radio 2");
    CHECK_INT_EQ(c.timeout_ms, 0);
    freq_source_config_fini(&c);

    CHECK_INT_EQ(parse_source("{\"Type\":\"n1mm\",\"RadioNr\":1,"
                              "\"StationName\":\" RUN-PC \","
                              "\"TimeoutSeconds\":1.5,"
                              "\"Address\":\"127.0.0.1:12061\","
                              "\"_comment\":\"run station\"}",
                              2, &c, &err),
                 0);
    CHECK_MEM_EQ(c.name, strlen(c.name), "N1MM RUN-PC radio 1");
    CHECK_INT_EQ(c.timeout_ms, 1500);
    freq_source_config_fini(&c);

    struct {
        const char *json;
        const char *field;
    } bad[] = {
        {"{\"Type\":\"n1mm\",\"PollMilliseconds\":100}", "PollMilliseconds"},
        {"{\"Type\":\"n1mm\",\"RadioNr\":-1}", "RadioNr"},
        {"{\"Type\":\"n1mm\",\"RadioNr\":\"one\"}", "RadioNr"},
        {"{\"Type\":\"n1mm\",\"Address\":\"12060\"}", "Address"},
        {"{\"Type\":\"n1mm\",\"Address\":\"n1mm-pc:12060\"}", "Address"},
        {"{\"Type\":\"n1mm\",\"TimeoutSeconds\":-1}", "TimeoutSeconds"},
        {"{\"Type\":\"n1mm\",\"Sorces\":[]}", "Sorces"},
        {"{\"Type\":\"flrig\"}", "Type"},
        {"{\"Type\":5}", "Type"},
        {"{}", "Type"},
    };
    for (size_t i = 0; i < ARRAY_LEN(bad); i++) {
        int e = parse_source(bad[i].json, 1, &c, &err);
        test_check_int(e, ERR_INVALID, bad[i].json, "ERR_INVALID", __FILE__,
                       __LINE__);
        test_check_mem(err.field, strlen(err.field), bad[i].field,
                       strlen(bad[i].field), bad[i].json, __FILE__, __LINE__);
    }
    CHECK_INT_EQ(parse_source("{\"Type\":\"n1mm\",\"PollMilliseconds\":100}",
                              1, &c, &err),
                 ERR_INVALID);
    CHECK_MEM_EQ(err.message, strlen(err.message),
                 "PollMilliseconds does not apply to n1mm");
    CHECK_INT_EQ(parse_source("{\"Type\":\"flrig\"}", 1, &c, &err),
                 ERR_INVALID);
    CHECK_MEM_EQ(err.message, strlen(err.message),
                 "Type \"flrig\" is not a known source; use \"n1mm\", "
                 "\"dxlog\", \"rigctld\" or \"n3fjp\"");
}

/* The antenna number and the F-key caption, when sent. */
static void
test_parse_antenna_and_caption(void)
{
    struct n1mm_radio_info info;

    CHECK_INT_EQ(parse("<RadioInfo><Antenna> 3 </Antenna><FunctionKeyCaption>"
                       " CQ \x1b</FunctionKeyCaption></RadioInfo>",
                       &info),
                 0);
    CHECK(info.have_antenna && info.antenna == 3);
    CHECK(info.have_caption);
    CHECK_MEM_EQ(info.caption, strlen(info.caption), "CQ ?");

    /* Not sent, empty, or not a plain number: no antenna. */
    CHECK_INT_EQ(parse("<RadioInfo><Freq>1</Freq></RadioInfo>", &info), 0);
    CHECK(!info.have_antenna && !info.have_caption);
    CHECK_INT_EQ(parse("<RadioInfo><Antenna></Antenna></RadioInfo>", &info),
                 0);
    CHECK(!info.have_antenna);
    CHECK_INT_EQ(parse("<RadioInfo><Antenna>-1</Antenna></RadioInfo>", &info),
                 0);
    CHECK(!info.have_antenna);

    /* An empty caption is a caption: the F-key message ended. */
    CHECK_INT_EQ(parse("<RadioInfo><FunctionKeyCaption/></RadioInfo>", &info),
                 0);
    CHECK(info.have_caption && info.caption[0] == '\0');

    /* A caption too long to keep is left out; the packet still counts. */
    char p[512];
    char caption[CAPS_N1MM_CAPTION + 10];
    memset(caption, 'x', sizeof(caption) - 1);
    caption[sizeof(caption) - 1] = '\0';
    snprintf(p, sizeof(p),
             "<RadioInfo><Freq>700000</Freq><FunctionKeyCaption>%s"
             "</FunctionKeyCaption></RadioInfo>",
             caption);
    CHECK_INT_EQ(parse(p, &info), 0);
    CHECK(!info.have_caption && info.freq == 700000);
}

/* FollowAntenna and SendFKeyText: off unless asked for. */
static void
test_logger_settings(void)
{
    struct freq_source_config c;
    struct freq_field_error err;

    CHECK_INT_EQ(parse_source("{\"Type\":\"n1mm\",\"FollowAntenna\":true,"
                              "\"SendFKeyText\":true}",
                              1, &c, &err),
                 0);
    cJSON *json = freq_source_to_json(&c);
    char *text = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
    CHECK(text != NULL && strstr(text, "\"FollowAntenna\":true,"
                                       "\"SendFKeyText\":true") != NULL);
    cJSON_free(text);
    cJSON_Delete(json);
    freq_source_config_fini(&c);

    /* Left out when off. */
    CHECK_INT_EQ(parse_source("{\"Type\":\"n1mm\",\"FollowAntenna\":false}", 1,
                              &c, &err),
                 0);
    json = freq_source_to_json(&c);
    text = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
    CHECK(text != NULL && strstr(text, "FollowAntenna") == NULL &&
          strstr(text, "SendFKeyText") == NULL);
    cJSON_free(text);
    cJSON_Delete(json);
    freq_source_config_fini(&c);

    CHECK_INT_EQ(parse_source("{\"Type\":\"dxlog\",\"FollowAntenna\":true}", 1,
                              &c, &err),
                 0);
    freq_source_config_fini(&c);
    CHECK_INT_EQ(parse_source("{\"Type\":\"dxlog\",\"SendFKeyText\":false}", 1,
                              &c, &err),
                 ERR_INVALID);
    CHECK_MEM_EQ(err.message, strlen(err.message),
                 "SendFKeyText does not apply to dxlog");
    CHECK_INT_EQ(
        parse_source("{\"Type\":\"n1mm\",\"FollowAntenna\":1}", 1, &c, &err),
        ERR_INVALID);
    CHECK_MEM_EQ(err.field, strlen(err.field), "FollowAntenna");
}

/* The dxlog type takes the same settings, under its own name. */
static void
test_dxlog_settings(void)
{
    struct freq_source_config c;
    struct freq_field_error err;

    CHECK_INT_EQ(parse_source("{\"Type\":\"DXLog\"}", 2, &c, &err), 0);
    CHECK(c.ops == &freq_dxlog_ops);
    CHECK_MEM_EQ(c.name, strlen(c.name), "DXLog radio 2");
    freq_source_config_fini(&c);

    CHECK_INT_EQ(parse_source("{\"Type\":\"dxlog\",\"RadioNr\":1,"
                              "\"StationName\":\"STN1\","
                              "\"TimeoutSeconds\":5}",
                              2, &c, &err),
                 0);
    CHECK_MEM_EQ(c.name, strlen(c.name), "DXLog STN1 radio 1");
    /* Written back as dxlog, with the defaults filled in. */
    cJSON *json = freq_source_to_json(&c);
    char *text = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK_MEM_EQ(text, strlen(text),
                     "{\"Type\":\"dxlog\",\"Address\":\"127.0.0.1:12060\","
                     "\"RadioNr\":1,\"StationName\":\"STN1\","
                     "\"TimeoutSeconds\":5}");
        cJSON_free(text);
    }
    cJSON_Delete(json);
    freq_source_config_fini(&c);

    CHECK_INT_EQ(parse_source("{\"Type\":\"dxlog\",\"PollMilliseconds\":1}", 1,
                              &c, &err),
                 ERR_INVALID);
    CHECK_MEM_EQ(err.message, strlen(err.message),
                 "PollMilliseconds does not apply to dxlog");
}

/* A free UDP port on the loopback interface. */
static void
free_udp_address(char *out, size_t size)
{
    struct plat_addr addr;
    plat_socket s;
    char text[PLAT_ADDR_TEXT_MAX];

    plat_addr_parse("127.0.0.1:0", 11, &addr);
    plat_udp_bind(&addr, &s);
    plat_socket_local_addr(s, &addr);
    plat_socket_close(s);
    plat_addr_format(&addr, text);
    snprintf(out, size, "%s", text);
}

static int
start_source(struct freq_env *env, const char *json, int moas_radio,
             struct freq_tracker *t, struct freq_source **out)
{
    struct freq_source_config c;
    struct freq_field_error err;
    struct freq_feed feed;

    int e = parse_source(json, moas_radio, &c, &err);
    if (e == 0) {
        e = freq_tracker_add(t, c.name, c.timeout_ms, &feed);
    }
    if (e == 0) {
        e = freq_source_start(env, &c, feed, out);
    }
    freq_source_config_fini(&c);
    return e;
}

/* Sends packets and runs the loop until want_khz shows, or time runs out. */
static bool
send_until(struct loop *loop, plat_socket tx, const struct plat_addr *to,
           const char *packet, struct freq_tracker *t, int want_khz)
{
    uint64_t end = plat_monotonic_ms() + 3000;
    while (plat_monotonic_ms() < end) {
        plat_sendto(tx, packet, strlen(packet), to);
        loop_run_once(loop, 20);
        int khz = 0;
        if (freq_tracker_choose(t, &khz, NULL) >= 0 && khz == want_khz) {
            return true;
        }
    }
    return false;
}

static void
test_listener(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *radio1;
    struct freq_tracker *radio2;
    struct freq_tracker *run_only;
    struct freq_source *s1;
    struct freq_source *s2;
    struct freq_source *s3;
    char address[PLAT_ADDR_TEXT_MAX];
    char json[256];
    char packet[2048];

    free_udp_address(address, sizeof(address));
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &radio1), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &radio2), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &run_only), 0);

    /* Three sources sharing one address. */
    snprintf(json, sizeof(json), "{\"Type\":\"n1mm\",\"Address\":\"%s\"}",
             address);
    CHECK_INT_EQ(start_source(env, json, 1, radio1, &s1), 0);
    CHECK_INT_EQ(start_source(env, json, 2, radio2, &s2), 0);
    snprintf(json, sizeof(json),
             "{\"Type\":\"n1mm\",\"Address\":\"%s\",\"RadioNr\":1,"
             "\"StationName\":\"run-pc\"}",
             address);
    CHECK_INT_EQ(start_source(env, json, 1, run_only, &s3), 0);

    struct plat_addr to;
    plat_socket tx;
    plat_addr_parse(address, strlen(address), &to);
    CHECK_INT_EQ(plat_udp_open(&to, &tx), 0);

    make_packet(packet, sizeof(packet), "MULT-PC", 1, 1402550, "True");
    CHECK(send_until(loop, tx, &to, packet, radio1, 14025));
    CHECK_INT_EQ(freq_tracker_choose(run_only, NULL, NULL), -1);
    CHECK_INT_EQ(freq_tracker_choose(radio2, NULL, NULL), -1);

    /* The station name matches without regard to case. */
    make_packet(packet, sizeof(packet), "RUN-PC", 1, 700500, "True");
    CHECK(send_until(loop, tx, &to, packet, run_only, 7005));

    /* Not connected: the source fails. */
    make_packet(packet, sizeof(packet), "MULT-PC", 2, 2102000, "False");
    plat_sendto(tx, packet, strlen(packet), &to);
    struct freq_status st;
    for (int i = 0; i < 100; i++) {
        loop_run_once(loop, 20);
        freq_tracker_status(radio2, &st, 1);
        if (st.state == FREQ_ERROR) {
            break;
        }
    }
    CHECK_INT_EQ(st.state, FREQ_ERROR);
    CHECK_MEM_EQ(st.error, strlen(st.error),
                 "N1MM says the radio is not connected");

    /* Every radio heard, followed or not, sorted. */
    struct freq_n1mm_heard heard[8];
    size_t n = freq_n1mm_heard(env, &to, heard, ARRAY_LEN(heard));
    CHECK_INT_EQ(n, 3);
    if (n == 3) {
        CHECK_MEM_EQ(heard[0].station_name, strlen(heard[0].station_name),
                     "MULT-PC");
        CHECK_INT_EQ(heard[0].radio_nr, 1);
        CHECK_INT_EQ(heard[0].khz, 14025);
        CHECK_INT_EQ(heard[1].radio_nr, 2);
        CHECK(!heard[1].connected);
        CHECK_MEM_EQ(heard[2].station_name, strlen(heard[2].station_name),
                     "RUN-PC");
    }

    /* Stopping every source closes the listener; the port is free again. */
    freq_source_stop(s1);
    freq_source_stop(s2);
    CHECK_INT_EQ(freq_n1mm_heard(env, &to, heard, ARRAY_LEN(heard)), 3);
    freq_source_stop(s3);
    CHECK_INT_EQ(freq_n1mm_heard(env, &to, heard, ARRAY_LEN(heard)), 0);
    plat_socket again;
    CHECK_INT_EQ(plat_udp_bind(&to, &again), 0);
    plat_socket_close(again);

    plat_socket_close(tx);
    freq_tracker_destroy(radio1);
    freq_tracker_destroy(radio2);
    freq_tracker_destroy(run_only);
    freq_env_destroy(env);
    loop_destroy(loop);
}

/*
 * N1MM and DXLog sending to the same address: one listener, and each
 * source follows only its own program's packets.
 */
static void
test_dxlog_beside_n1mm(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *n1mm;
    struct freq_tracker *dxlog;
    struct freq_tracker *stn2;
    struct freq_source *s1;
    struct freq_source *s2;
    struct freq_source *s3;
    char address[PLAT_ADDR_TEXT_MAX];
    char json[256];
    char packet[2048];
    int khz = 0;

    free_udp_address(address, sizeof(address));
    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &n1mm), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &dxlog), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &stn2), 0);

    snprintf(json, sizeof(json), "{\"Type\":\"n1mm\",\"Address\":\"%s\"}",
             address);
    CHECK_INT_EQ(start_source(env, json, 1, n1mm, &s1), 0);
    snprintf(json, sizeof(json), "{\"Type\":\"dxlog\",\"Address\":\"%s\"}",
             address);
    CHECK_INT_EQ(start_source(env, json, 1, dxlog, &s2), 0);
    snprintf(json, sizeof(json),
             "{\"Type\":\"dxlog\",\"Address\":\"%s\",\"RadioNr\":1,"
             "\"StationName\":\"stn2\"}",
             address);
    CHECK_INT_EQ(start_source(env, json, 1, stn2, &s3), 0);

    struct plat_addr to;
    plat_socket tx;
    plat_addr_parse(address, strlen(address), &to);
    CHECK_INT_EQ(plat_udp_open(&to, &tx), 0);

    /* N1MM's packet reaches only the n1mm source. */
    make_packet(packet, sizeof(packet), "RUN-PC", 1, 1402550, "True");
    CHECK(send_until(loop, tx, &to, packet, n1mm, 14025));
    CHECK_INT_EQ(freq_tracker_choose(dxlog, NULL, NULL), -1);

    /* DXLog's reaches only the dxlog sources whose station matches. */
    make_dxlog_packet(packet, sizeof(packet), "STN1", 1, 2134500);
    CHECK(send_until(loop, tx, &to, packet, dxlog, 21345));
    CHECK_INT_EQ(freq_tracker_choose(stn2, NULL, NULL), -1);
    CHECK(freq_tracker_choose(n1mm, &khz, NULL) >= 0 && khz == 14025);
    /* STN2 is followed by both: only one asked for a station. */
    make_dxlog_packet(packet, sizeof(packet), "STN2", 1, 700500);
    CHECK(send_until(loop, tx, &to, packet, stn2, 7005));
    CHECK(freq_tracker_choose(dxlog, &khz, NULL) >= 0 && khz == 7005);

    /* A packet that does not say who sent it is taken to be N1MM's. */
    CHECK(send_until(loop, tx, &to,
                     "<RadioInfo><RadioNr>1</RadioNr>"
                     "<Freq>1803000</Freq></RadioInfo>",
                     n1mm, 18030));
    CHECK(freq_tracker_choose(dxlog, &khz, NULL) >= 0 && khz == 7005);

    /* Another program's packets reach neither, but are heard. */
    const char other[] = "<RadioInfo><app>Other</app><RadioNr>1</RadioNr>"
                         "<Freq>350000</Freq></RadioInfo>";
    struct freq_n1mm_heard heard[8];
    size_t n = 0;
    for (int i = 0; i < 100 && n < 5; i++) {
        plat_sendto(tx, other, sizeof(other) - 1, &to);
        loop_run_once(loop, 20);
        n = freq_n1mm_heard(env, &to, heard, ARRAY_LEN(heard));
    }
    CHECK(freq_tracker_choose(n1mm, &khz, NULL) >= 0 && khz == 18030);
    CHECK(freq_tracker_choose(dxlog, &khz, NULL) >= 0 && khz == 7005);

    /* Heard, by program and then station. */
    CHECK_INT_EQ(n, 5);
    if (n == 5) {
        CHECK_MEM_EQ(heard[0].program, strlen(heard[0].program), "");
        CHECK_MEM_EQ(heard[1].program, strlen(heard[1].program), "DXLog");
        CHECK_MEM_EQ(heard[1].station_name, strlen(heard[1].station_name),
                     "STN1");
        CHECK_MEM_EQ(heard[2].station_name, strlen(heard[2].station_name),
                     "STN2");
        CHECK_MEM_EQ(heard[3].program, strlen(heard[3].program), "N1MM");
        CHECK_MEM_EQ(heard[4].program, strlen(heard[4].program), "other");
    }

    freq_source_stop(s1);
    freq_source_stop(s2);
    freq_source_stop(s3);
    plat_socket_close(tx);
    freq_tracker_destroy(n1mm);
    freq_tracker_destroy(dxlog);
    freq_tracker_destroy(stn2);
    freq_env_destroy(env);
    loop_destroy(loop);
}

static void
test_address_in_use(void)
{
    struct loop *loop;
    struct freq_env *env;
    struct freq_tracker *t;
    struct freq_source *s;
    struct plat_addr addr;
    plat_socket taken;
    char text[PLAT_ADDR_TEXT_MAX];
    char json[256];

    /* Something else holds the port. */
    plat_addr_parse("127.0.0.1:0", 11, &addr);
    CHECK_INT_EQ(plat_udp_bind(&addr, &taken), 0);
    plat_socket_local_addr(taken, &addr);
    plat_addr_format(&addr, text);

    CHECK_INT_EQ(loop_create(&loop), 0);
    CHECK_INT_EQ(freq_env_create(loop, &env), 0);
    CHECK_INT_EQ(freq_tracker_create(loop, NULL, NULL, &t), 0);
    snprintf(json, sizeof(json), "{\"Type\":\"n1mm\",\"Address\":\"%s\"}",
             text);
    CHECK_INT_EQ(start_source(env, json, 1, t, &s), 0);

    struct freq_status st;
    freq_tracker_status(t, &st, 1);
    CHECK_INT_EQ(st.state, FREQ_ERROR);
    CHECK(strstr(st.error, "cannot listen on 127.0.0.1:") != NULL);

    /* Once the port is free, the retry succeeds and the error clears. */
    plat_socket_close(taken);
    struct plat_addr to = addr;
    plat_socket tx;
    CHECK_INT_EQ(plat_udp_open(&to, &tx), 0);
    char packet[2048];
    make_packet(packet, sizeof(packet), "PC", 1, 350000, "True");
    CHECK(send_until(loop, tx, &to, packet, t, 3500) ||
          send_until(loop, tx, &to, packet, t, 3500));
    plat_socket_close(tx);

    freq_source_stop(s);
    freq_tracker_destroy(t);
    freq_env_destroy(env);
    loop_destroy(loop);
}

int
main(void)
{
    if (plat_net_init() != 0) {
        return 1;
    }
    RUN(test_parse_packets);
    RUN(test_parse_apps);
    RUN(test_refuses_other_packets);
    RUN(test_settings);
    RUN(test_dxlog_settings);
    RUN(test_parse_antenna_and_caption);
    RUN(test_logger_settings);
    RUN(test_listener);
    RUN(test_dxlog_beside_n1mm);
    RUN(test_address_in_use);
    plat_net_fini();
    return test_summary();
}
