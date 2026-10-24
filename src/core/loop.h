/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */

/*
 * The event loop.  Everything that waits for a socket or a timer registers
 * a callback here, and the loop calls it when the time comes.  There is one
 * thread, so callbacks never run at the same time.
 *
 * Callbacks may do anything to the loop: watch or unwatch any socket, start
 * or stop any timer, or stop the loop.  A socket that is unwatched (or a
 * timer that is stopped) during a pass gets no further callbacks in that
 * pass, even if it was already found ready, so a callback can safely free
 * whatever another callback would have used.
 */

#ifndef MOAS_CORE_LOOP_H
#define MOAS_CORE_LOOP_H

#include "core/common.h"
#include "platform/net.h"

struct loop;

/* What a socket is watched for, and what it is reported as. */
#define LOOP_READ 0x1  /* readable, or a connection to accept */
#define LOOP_WRITE 0x2 /* writable, or a connect has finished */
/*
 * Reported only: an error, or the other end hung up.  It always comes with
 * LOOP_READ for a socket watched for reading, on every system, so reading
 * the socket shows the end of the stream or the error.
 */
#define LOOP_ERROR 0x4

typedef void loop_io_fn(struct loop *loop, plat_socket sock, int events,
                        void *arg);
typedef void loop_timer_fn(struct loop *loop, void *arg);

/*
 * A timer.  It lives inside the struct that uses it; set it up with
 * loop_timer_init().  The fields are the loop's.
 */
struct loop_timer {
    struct loop_timer *next;
    loop_timer_fn *fn;
    void *arg;
    uint64_t deadline;   /* monotonic milliseconds */
    uint64_t repeat_ms;  /* 0 for a one-shot timer */
    uint64_t armed_pass; /* the pass in which it was last (re)started */
    bool active;
};

int loop_create(struct loop **out);

/* Frees the loop.  Watched sockets are not closed; timers are forgotten. */
void loop_destroy(struct loop *loop);

/*
 * Watches sock for events (LOOP_READ and/or LOOP_WRITE; 0 keeps the
 * registration but waits for nothing).  Watching a socket that is already
 * watched replaces its events, callback and argument.  ERR_LIMIT if
 * CAPS_LOOP_WATCHES sockets are already watched.
 */
int loop_watch(struct loop *loop, plat_socket sock, int events, loop_io_fn *fn,
               void *arg);

/* Stops watching sock.  Does nothing if it is not watched. */
void loop_unwatch(struct loop *loop, plat_socket sock);

void loop_timer_init(struct loop_timer *timer, loop_timer_fn *fn, void *arg);

/*
 * Starts (or restarts) timer to fire after delay_ms, then every repeat_ms
 * if that is not 0.  A timer started from a callback fires in a later pass
 * at the earliest, even with a delay of 0.
 */
void loop_timer_start(struct loop *loop, struct loop_timer *timer,
                      uint64_t delay_ms, uint64_t repeat_ms);

/* Stops timer.  Does nothing if it is not running. */
void loop_timer_stop(struct loop *loop, struct loop_timer *timer);

bool loop_timer_active(const struct loop_timer *timer);

/*
 * Runs until loop_stop() is called, or until there is nothing left to wait
 * for (no sockets waiting for events and no timers).  Returns 0, or an
 * error if waiting for sockets fails.
 */
int loop_run(struct loop *loop);

/*
 * One pass: fires due timers, waits up to max_wait_ms (-1: as long as
 * needed) for sockets or the next timer, and calls the callbacks of the
 * sockets that are ready.  For tests and for callers with loops of their
 * own.
 */
int loop_run_once(struct loop *loop, int max_wait_ms);

/* Makes loop_run() return after the current callback. */
void loop_stop(struct loop *loop);

/* The monotonic time in milliseconds at the start of the current wait. */
uint64_t loop_now(const struct loop *loop);

#endif /* MOAS_CORE_LOOP_H */
