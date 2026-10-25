/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Reading the RadioInfo packets that N1MM Logger+ and DXLog.net broadcast.
 * A packet looks like this, with more fields:
 *
 *   <?xml version="1.0" encoding="utf-8"?>
 *   <RadioInfo>
 *     <app>N1MM</app>
 *     <StationName>RUN-PC</StationName>
 *     <RadioNr>1</RadioNr>
 *     <Freq>1402550</Freq>
 *     <IsConnected>True</IsConnected>
 *   </RadioInfo>
 *
 * Freq is in units of 10 Hz.  DXLog sends the same layout with <app>DXLog
 * </app>, names the station in <Station> rather than <StationName>, and has
 * no IsConnected.
 *
 * This is not a general XML parser: it accepts the flat layout these
 * programs send (an optional declaration, then <RadioInfo> holding simple
 * elements with text) and nothing else, which keeps the code that reads
 * packets from the network small.  Other kinds of packets sent to the same
 * port are not RadioInfo and are refused.
 */

#ifndef MOAS_FREQ_N1MM_XML_H
#define MOAS_FREQ_N1MM_XML_H

#include "core/caps.h"
#include "core/common.h"
#include "core/str.h"

/* Which program sent a packet, from its <app> element. */
enum n1mm_app {
    N1MM_APP_UNKNOWN, /* no <app> */
    N1MM_APP_N1MM,
    N1MM_APP_DXLOG,
    N1MM_APP_OTHER, /* some other program */
};

struct n1mm_radio_info {
    enum n1mm_app app;
    char station_name[CAPS_N1MM_STATION]; /* trimmed and cleaned */
    int radio_nr;                         /* 0 if missing */
    int64_t freq;                         /* 10 Hz units; 0 if missing */
    bool connected; /* true unless IsConnected is false */
    /* The logger's antenna number (N1MM's Alt-F9 changes it), if sent. */
    int antenna;
    bool have_antenna;
    /* The caption of the F-key message being sent, if sent; cleaned. */
    char caption[CAPS_N1MM_CAPTION];
    bool have_caption;
};

/*
 * Reads one packet.  ERR_INVALID if it is not a RadioInfo packet in the
 * layout above, or a number in it is not a number.
 */
int n1mm_parse_radio_info(struct str packet, struct n1mm_radio_info *out);

/*
 * The frequency in whole kHz, rounded down so that a radio just below a
 * band edge is not reported inside the band.
 */
int n1mm_khz(const struct n1mm_radio_info *info);

#endif /* MOAS_FREQ_N1MM_XML_H */
