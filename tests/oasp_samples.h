/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Sample messages in the form MOAS Server v2.0.2 sends them, shortened,
 * with generic antenna names.  The same samples as the Go version's tests.
 */

#ifndef MOAS_TESTS_OASP_SAMPLES_H
#define MOAS_TESTS_OASP_SAMPLES_H

#define SAMPLE_SERVER                                                         \
    "{ \"Message\" : \"Server\", \"Sequence\" : 1, \"Version\" : 1, "         \
    "\"Parameters\" : { \"Name\" : \"MOAS Server (M1DST) v2.0.2\", "          \
    "\"Rows\" : 6, \"Columns\" : 5, \"TXRX\" : true } }"

#define SAMPLE_ROTATORS                                                       \
    "{ \"Message\" : \"Rotators\", \"Sequence\" : 2, \"Version\" : 1, "       \
    "\"Parameters\" : { \"Rotators\" : [] } }"

#define SAMPLE_CONFIGURATION                                                  \
    "{ \"Message\" : \"Configuration\", \"Sequence\" : 3, \"Version\" : 1, "  \
    "\"Parameters\" : { \"Station\" : 1, \"Radio\" : 2, "                     \
    "\"LowerFrequency\" : 1800, \"UpperFrequency\" : 2000, \"Antennas\" : [ " \
    "{ \"Antenna\" : 60, \"ShortName\" : \"160m Inverted L\", "               \
    "\"LongName\" : \"160m Inverted L\", \"Description\" : [ { "              \
    "\"Manufacturer\" : \"HomeBrew\", \"Model\" : \"Inverted L\", "           \
    "\"Type\" : \"Vertical\", \"Elements\" : 1, \"Lobes\" : 1, "              \
    "\"Direction\" : 0 } ], \"Row\" : 0, \"Column\" : 0 }, "                  \
    "{ \"Antenna\" : 50, \"ShortName\" : \"Bev-NE\", "                        \
    "\"LongName\" : \"Northeast Beverage\", \"Description\" : [ { "           \
    "\"Manufacturer\" : \"Homebrew\", \"Model\" : \"Beverage\", "             \
    "\"Type\" : \"beverage\", \"Elements\" : 1, \"3dBWidth\" : 80, "          \
    "\"Lobes\" : 1, \"Direction\" : 0 } ], \"Row\" : 0, \"Column\" : 4 } ], " \
    "\"Functions\" : [ { \"Function\" : 0, \"ShortName\" : \"Swap\", "        \
    "\"LongName\" : \"Swap antennas between radios 1 and 2\", "               \
    "\"Momentary\" : true, \"Alternate\" : false, \"Row\" : 5, "              \
    "\"Column\" : 0 } ] } }"

#define SAMPLE_STATE                                                          \
    "{ \"Message\" : \"State\", \"Sequence\" : 4, \"Version\" : 1, "          \
    "\"Parameters\" : { \"Station\" : 1, \"Radio\" : 2, "                     \
    "\"TXShortName\" : \"160m Inverted L\", "                                 \
    "\"TXLongName\" : \"160m Inverted L\", \"TXId\" : 60, "                   \
    "\"RXShortName\" : \"Bev-NE\", \"RXLongName\" : \"Northeast Beverage\", " \
    "\"RXId\" : 50, \"Antennas\" : [ { \"Antenna\" : 60, \"TX\" : 2, "        \
    "\"RX\" : 1 }, { \"Antenna\" : 50, \"TX\" : 0, \"RX\" : 2 } ], "          \
    "\"Functions\" : [ { \"Function\" : 0, \"State\" : 0 } ] } }"

#define SAMPLE_TR                                                             \
    "{ \"Message\" : \"TR\", \"Sequence\" : 5, \"Version\" : 1, "             \
    "\"Parameters\" : { \"Station\" : 1, \"Radio\" : 2, \"TR\" : 3 } }"

/* A message no version of this library knows. */
#define SAMPLE_SURPRISE                                                       \
    "{ \"Message\" : \"Surprise\", \"Sequence\" : 6, \"Version\" : 1, "       \
    "\"Parameters\" : { \"X\" : 1 } }"

/* A TR message whose TR is not a number: skipped, the stream carries on. */
#define SAMPLE_BAD_TR                                                         \
    "{ \"Message\" : \"TR\", \"Sequence\" : 7, \"Version\" : 1, "             \
    "\"Parameters\" : { \"TR\" : \"not a number\" } }"

#endif /* MOAS_TESTS_OASP_SAMPLES_H */
