/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * Every hard limit in the program, in one place so that they can all be
 * reviewed together.  Input larger than a limit is refused, not truncated,
 * unless a comment says otherwise.  Later stages add their limits here.
 */

#ifndef MOAS_CORE_CAPS_H
#define MOAS_CORE_CAPS_H

/* Sockets one event loop can watch at once (at most PLAT_POLL_MAX). */
#define CAPS_LOOP_WATCHES 128

/* Longest log line in bytes; longer lines are cut short and marked. */
#define CAPS_LOG_LINE 1024

/* Largest settings file read, in bytes. */
#define CAPS_SETTINGS_FILE (1024 * 1024)

/* Longest file path, in bytes of UTF-8, including the terminating NUL. */
#define CAPS_PATH 4096

/*
 * Deepest nesting of objects and arrays in any JSON document.  cJSON is
 * built with the same limit (third_party/CMakeLists.txt); core/json.c
 * checks that the two agree.
 */
#define CAPS_JSON_DEPTH 32

/* Largest single message from a MOAS server, in bytes. */
#define CAPS_OASP_MESSAGE (256 * 1024)

/* Most bytes waiting to be sent to a MOAS server. */
#define CAPS_OASP_SEND_QUEUE (64 * 1024)

/*
 * Longest text field in a MOAS message (a name, a description), in bytes of
 * UTF-8, including the terminating NUL.
 */
#define CAPS_OASP_TEXT 128

/* Longest message name ("Configuration"), including the NUL. */
#define CAPS_OASP_MESSAGE_NAME 32

/* Most antennas, functions and radios in one MOAS message. */
#define CAPS_OASP_ANTENNAS 128
#define CAPS_OASP_FUNCTIONS 64
#define CAPS_OASP_RADIOS 16

/* Most descriptions of one antenna (there is usually one). */
#define CAPS_OASP_DESCRIPTIONS 4

#endif /* MOAS_CORE_CAPS_H */
