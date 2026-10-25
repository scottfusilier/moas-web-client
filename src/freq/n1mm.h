/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The "n1mm" and "dxlog" source types: follow the RadioInfo packets that
 * N1MM Logger+ and DXLog.net broadcast over UDP, in the same format.  Each
 * type follows only its own program's packets, so both can send to the same
 * address.  Sources that use the same address, of either type, share one
 * listener.  Settings, the same for both:
 *
 *   Address      where to listen (default 127.0.0.1:12060, the default of
 *                both programs)
 *   RadioNr      the program's number for the radio (default: the MOAS
 *                radio's)
 *   StationName  accept only packets from this station (default: any);
 *                N1MM's StationName, DXLog's Station
 *   TimeoutSeconds  readings older than this are stale (default: never,
 *                because N1MM only sends when something changes)
 *
 * If the address cannot be opened, every source on it fails and the
 * listener tries again every 2 seconds.
 */

#ifndef MOAS_FREQ_N1MM_H
#define MOAS_FREQ_N1MM_H

#include "core/caps.h"
#include "core/common.h"
#include "freq/source.h"
#include "platform/net.h"

#define FREQ_N1MM_DEFAULT_ADDRESS "127.0.0.1:12060"

extern const struct freq_source_ops freq_n1mm_ops;
extern const struct freq_source_ops freq_dxlog_ops;

/* One radio reported on an address, whether followed or not. */
struct freq_n1mm_heard {
    const char *program; /* "N1MM", "DXLog", "other", or "" if not said */
    char station_name[CAPS_N1MM_STATION];
    int radio_nr;
    int khz;
    bool connected;
    uint64_t at_ms; /* monotonic time of the last packet */
};

/*
 * The radios heard on addr since its listener started, by program, station
 * name and then radio number.  Returns how many were written to out (at
 * most max); 0 if nothing listens on addr.  Helps when choosing RadioNr.
 */
size_t freq_n1mm_heard(struct freq_env *env, const struct plat_addr *addr,
                       struct freq_n1mm_heard *out, size_t max);

#endif /* MOAS_FREQ_N1MM_H */
