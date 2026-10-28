/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Reading what an N3FJP program sends on its TCP API: one message per line,
 * such as
 *
 *   <CMD><READBMFRESPONSE><BAND>20</BAND><MODE>CW</MODE>
 *   <MODETEST>CW</MODETEST><FREQ>14.025000</FREQ></CMD>
 *
 * (on one line), with the frequency in MHz.  Only the frequency is used.
 */

#ifndef MOAS_FREQ_N3FJP_REPLY_H
#define MOAS_FREQ_N3FJP_REPLY_H

#include "core/common.h"
#include "core/str.h"

/*
 * Reads one line (without its newline).  Returns 0 with the frequency in
 * whole kHz, rounded down, for a READBMFRESPONSE; ERR_NOT_FOUND for a blank
 * line or any other command, which carries no frequency; ERR_RANGE for a
 * frequency below 1 kHz (including an empty or zero one) or above 1 THz;
 * ERR_INVALID for anything else.
 */
int n3fjp_parse_line(struct str line, int *khz);

#endif /* MOAS_FREQ_N3FJP_REPLY_H */
