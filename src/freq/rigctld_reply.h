/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Reading hamlib rigctld's reply to the "f" (get frequency) command: one
 * line holding the frequency in Hz ("14025000", or "14025000.000000" from
 * some versions), or "RPRT <code>" when the radio cannot be read.
 */

#ifndef MOAS_FREQ_RIGCTLD_REPLY_H
#define MOAS_FREQ_RIGCTLD_REPLY_H

#include "core/common.h"
#include "core/str.h"

/*
 * Reads one reply line (without its newline).  Returns 0 with the
 * frequency in whole kHz, rounded down; ERR_IO with rigctld's error code in
 * *report for an RPRT line; ERR_RANGE for a frequency below 1 kHz or above
 * 1 THz; ERR_INVALID for anything else.
 */
int rigctld_parse_reply(struct str line, int *khz, int *report);

#endif /* MOAS_FREQ_RIGCTLD_REPLY_H */
