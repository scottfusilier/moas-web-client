# moas-web-client roadmap

This is the plan for moas-web-client, the C version of moasweb, which
replaces the earlier Go version.  The project is moas-web-client; the
program it builds is `moasweb`.  The Go version is the reference until this
one matches it; after that it is retired.

## Decisions

* C11, built with CMake (presets, CTest, CPack).  C because the maintainer
  knows it; the bounded buffer type, paired create and destroy functions, one
  thread, hard limits, sanitizers and fuzzing provide the safety; a C `oasp`
  library can be called from almost any language; C suits small hardware;
  and both vendored libraries are C.
* Meant to run on Windows, Linux, FreeBSD and macOS.  Until the project is
  closer to complete, it is built and tested on Windows, FreeBSD and Linux;
  macOS and MSVC come later.  Ninja is the build tool on every system, so
  the presets and check script are the same everywhere.
* Developed locally, with no hosting service assumed.  Checks run from a
  script (`scripts/check.sh`, `scripts\check.bat`) rather than a hosted
  service.
* Licensed BSD-2-Clause.
* Code layout follows PEP 7: 4-space indents, no tabs, braces always,
  declarations where first used, return type on its own line, 79-column
  lines.  Names are `lower_snake_case` with a module prefix (`plat_`,
  `loop_`, `buf_`, `oasp_`, `freq_`, `http_`, `app_`), and macros and
  constants are `UPPER_CASE`.
* Two third-party libraries, both kept in `third_party/` at a pinned version
  with their licenses:
  * cJSON (MIT) for JSON;
  * picohttpparser (MIT) for parsing HTTP requests.  The server, routing and
    server-sent events are our own.
* Any further dependency must have a license compatible with BSD-2-Clause
  (MIT, BSD or ISC; no GPL, and LGPL avoided), be small enough to vendor and
  read, be actively maintained and widely used, and build cleanly on gcc,
  clang and MSVC with our warnings.  The reason for each one is recorded in
  `third_party/`.
* One thread and one event loop.
* Every input from outside the program goes through one small parser with
  hard limits, and every parser has a fuzz target.
* Built with strict warnings, treated as errors by the presets, and with
  hardening flags; tests run under AddressSanitizer and UndefinedBehaviorSanitizer.
* Abstraction where it pays: frequency sources behind one interface and a
  registry, the event loop, the `oasp` library standing on its own, a route
  table for the web server, and one platform layer that holds all the
  system-specific code.

## Layers

```
                 app/  (moasweb)
                   │
       ┌───────────┼────────────┐
       ▼           ▼            ▼
     oasp/       freq/        http/      (independent of each other)
       │           │            │
       └───────────┼────────────┘
                   ▼
                 core/
                   │
                   ▼
               platform/
```

Each layer is its own static library in CMake (`moas_platform`,
`moas_core`, `moas_oasp`, `moas_freq`, `moas_http`), linked only against the
layers below it, so the build enforces the layering.  `oasp/`, `freq/` and
`http/` do not know about each other; only `app/` wires them together.
Tests link only the layers they need.

## Layout

```
moas-web-client/
├── CMakeLists.txt, CMakePresets.json
├── LICENSE, README.md, CHANGELOG.md, CONTRIBUTING.md
├── .editorconfig, .clang-format, .gitattributes, .gitignore
├── cmake/   platform  warnings  hardening  sanitizers  format  check_banned  embed (.cmake)
├── scripts/ check.sh, check.bat   build and test every preset, check formatting
│            fuzz.sh               build with libFuzzer and fuzz each target (Unix)
├── src/
│   ├── platform/                  prefix plat_; the only #ifdef _WIN32
│   │   ├── err.h, err.c                           error codes shared by every layer
│   │   ├── net.h, net_addr.c, net_posix.c, net_win.c   addresses, sockets, poll
│   │   ├── net_pair.c                             a connected pair of loopback sockets
│   │   ├── fs.h, fs_posix.c, fs_win.c             capped reads, replace in one step, settings folder
│   │   ├── clock.h, clock_posix.c, clock_win.c    monotonic and wall-clock time
│   │   ├── proc.h, proc_posix.c, proc_win.c       UTF-8 arguments and console on Windows
│   │   └── signal.h, signal_posix.c, signal_win.c Ctrl+C and SIGTERM wake the loop
│   ├── core/
│   │   ├── common.h               included first everywhere: shared types and attributes
│   │   ├── caps.h                 every hard limit
│   │   ├── checked.h              overflow-checked size arithmetic
│   │   ├── str.h, str.c           read-only slices; range-checked number parsing
│   │   ├── buf.h, buf.c           growable buffer with a hard maximum
│   │   ├── log.h, log.c           logging
│   │   ├── json.h, json.c         typed, range-checked access over cJSON; writing JSON
│   │   └── loop.h, loop.c         event loop: sockets, timers, deferred calls
│   ├── oasp/                      prefix oasp_; usable on its own
│   │   ├── oasp.h                 public types and message structs
│   │   ├── frame.h, frame.c       object boundaries in the stream (no I/O, fuzzed)
│   │   ├── message.c              decode and encode (no I/O, fuzzed)
│   │   └── client.h, client.c     connect, Hello, send, reconnect with back-off
│   ├── freq/                      prefix freq_
│   │   ├── source.h               the interface every source type implements
│   │   ├── registry.c             type name to operations
│   │   ├── tracker.h, tracker.c   priority, failover, manual override, status
│   │   ├── n1mm.c, n1mm_xml.c     UDP listener; RadioInfo reader (fuzzed)
│   │   └── rigctld.c, rigctld_reply.c   poller and Test; reply parser (fuzzed)
│   ├── http/                      prefix http_
│   │   ├── http.h                 the interface: routes, replies, streams, server
│   │   ├── parse.h, parse.c       request heads: picohttpparser plus our checks (fuzzed)
│   │   ├── router.c, internal.h   method + path to handler; per-route body limit, local only
│   │   ├── guard.c                Host, Origin, Content-Type and local-only checks
│   │   └── server.c               connections, responses with security headers,
│   │                              replies sent later, server-sent events, limits
│   └── app/                       prefix app_; moasweb
│       ├── main.c                 startup order and clean shutdown
│       ├── args.h, args.c         command line; -config and --config
│       ├── settings.h, settings.c       settings, defaults, validation by field
│       ├── settings_file.c        lookup order, load, save in one step
│       ├── link.h, link.c         server connection as the page sees it
│       ├── snapshot.c             the JSON the page reads
│       ├── web.c                  route handlers
│       ├── runner.c               applies saved settings live
│       ├── watch.c                -watch: prints what the server sends, changes nothing
│       └── page.html              embedded at build time by cmake/embed.cmake
├── tests/
│   ├── test.h                     harness macros
│   ├── fake_moas_server.c/.h      scripted MOAS server on loopback, same event loop
│   ├── oasp_samples.h             sample server messages shared by tests and corpus
│   ├── fake_rigctld.c, n1mm_send.c
│   └── test_<module>.c            one per module
├── fuzz/
│   ├── fuzz.h                     the entry point every target defines
│   ├── fuzz_frame.c  fuzz_message.c  fuzz_n1mm_xml.c  fuzz_rigctld_reply.c
│   │   fuzz_http_parse.c  fuzz_settings.c
│   ├── replay_main.c              runs stored inputs as tests where libFuzzer is unavailable
│   └── corpus/<target>/
├── third_party/cjson/, third_party/picohttpparser/   each with its LICENSE; versions,
│                                  hashes and reasons in third_party/README.md
└── docs/    ROADMAP.md  architecture.md  protocol.md  adding-a-frequency-source.md
```

Parsing of outside input is always in its own file with no I/O, so that each
parser can be fuzzed alone and the risky code is easy to find.

Web routes match the Go version, so `page.html` is unchanged: `GET /`,
`GET /events`, `GET /api/state`, `POST /api/frequency`, `POST /api/select`,
`POST /api/function`, and, local only, `GET` and `POST /api/settings`,
`GET /api/n1mm-heard` and `POST /api/rigctld-test`.

## Rules that apply everywhere

* Every type has `x_create` and `x_destroy`; whoever creates something
  destroys it.  Small types that live inside other structs (such as
  `struct buf` and `struct loop_timer`) have `x_init` and `x_fini` instead.
  Callbacks may borrow data during the call but never keep it.
* Functions return 0 or an error code from `platform/err.h`; results come
  back through output parameters.
* Files, folders and environment variables are reached only through the
  platform layer, never the C library's `fopen()`, `remove()`, `getenv()`
  and the like, which on Windows do not take UTF-8.  The
  `banned_functions` test enforces this outside `src/platform/`.
* Untrusted text stays in slices until it has been checked, and only then is
  copied into owned memory.
* Every limit is in `caps.h`.
* `#ifdef _WIN32` appears only in `platform/`.
* macOS must stay possible even though it is not tested until late: the
  POSIX platform code uses only interfaces that FreeBSD, Linux and macOS
  share, and nothing specific to one of them.  In particular: no `epoll`,
  `accept4`, `SOCK_NONBLOCK`, `SOCK_CLOEXEC` or `MSG_NOSIGNAL` (Linux), and
  no `kqueue` (BSD and macOS); sockets are made non-blocking with `fcntl`,
  and a closed connection must not raise `SIGPIPE` (`SO_NOSIGPIPE` where
  available, otherwise `SIGPIPE` ignored).  Feature macros are set so that
  each system's headers declare what is used (`_POSIX_C_SOURCE`, plus
  `_DARWIN_C_SOURCE` on macOS).  The macOS settings folder is
  `~/Library/Application Support/moasweb/`.
* Frequency source types are added by writing one file and one registry line;
  each type checks its own settings.

## How a stage is done

A stage is done when the check script passes on Windows, FreeBSD and Linux:
every preset builds with warnings as errors, every test passes, including
under the sanitizers, and the formatting is clean.  Fuzz targets are run on
FreeBSD.  The maintainer runs the FreeBSD and Linux checks.

Each stage comes with a short summary: what is in it, where to start
reading, and anything uncertain.  It is reviewed and committed to the local
repository before the next stage starts.  A stage is roughly 500 to 1,500
lines.

## Stage 0: foundations

* The top of the layout above; each directory is added by the stage that
  first puts code in it.
* CMake: presets `debug`, `release` and `asan`; modules for warnings,
  warnings as errors, hardening and sanitizers on gcc, clang and MSVC; and
  `format` and `format-check` targets.
* A `banned_functions` test that fails if our code calls `strcpy`,
  `sprintf` or similar functions (replacing the compile-time ban planned for
  Stage 1, which would also have caught system headers).
* `.clang-format` (PEP 7 layout), `.editorconfig`, `.gitattributes`.
* `LICENSE`, `README.md`, `CONTRIBUTING.md` (style, building, testing),
  `CHANGELOG.md`.
* cJSON in `third_party/cjson/` and picohttpparser in
  `third_party/picohttpparser/`, each with its license and version, built and
  tested on Windows, FreeBSD and Linux with the sanitizers.  Windows is the
  least-tested system for picohttpparser upstream, so it is confirmed here.
* A small test harness (assert macros run by CTest), no extra dependency.
* `scripts/check.sh` and `scripts\check.bat`: build and test every preset
  and check the formatting.

Done when a placeholder program and one test build and pass, and the check
script passes, on Windows, FreeBSD and Linux.

Left for later stages: the `fuzz` preset arrives with the first parser in
Stage 2; static analysis (clang-tidy) is added once there is enough code for
it to look at.

## Stage 1: core and platform layer

* `core/`: bounded buffer and string type, checked size arithmetic,
  range-checked number parsing, logging, and one header holding every size
  limit.
* `platform/`: socket start-up, non-blocking TCP connect and listen, UDP,
  `poll` / `WSAPoll`, a monotonic clock, the settings folder, and replacing a
  file in one step.
* `core/loop`: the event loop; anything with a socket or a timer registers
  callbacks with it.

Tests: buffer edge cases, overflow checks, and the loop driving loopback
sockets and timers.

The platform layer is where macOS problems would show up, so it follows the
macOS rule under "Rules that apply everywhere".  If a Mac is available when
this stage is done, one run of `scripts/check.sh` on it is worth doing;
otherwise macOS is tested before 1.0, and any fixes stay in `platform/`.

As built, Stage 1 differs from the plan in a few ways:

* The error codes are in `platform/err.h`, the lowest layer, so the
  platform layer can return them too.
* Signal handling (Ctrl+C, SIGTERM) moved to Stage 2, where watch mode
  first needs it.
* Addresses are numeric (`192.168.1.10:12059`, `[::1]:8080`) or
  `localhost`; host names are not looked up yet, because looking one up
  blocks, and the program has one thread.  Host names are not needed for
  now (maintainer's decision); if they are later, the lookup has to happen
  off the event loop.
* On a hang-up or error the loop also reports the socket readable on
  every system (Windows reports only the hang-up), so reading shows what
  happened.

## Stage 2: the OASP protocol and watch mode

* Signal handling: Ctrl+C and SIGTERM wake the event loop so the program
  can stop cleanly.
* Framer: finds where each JSON object ends in the server's stream of objects
  sent back to back, allowing for braces inside strings.  Fuzzed.
* Messages: decoded into C structs through cJSON with depth and size limits;
  the client's messages encoded.
* Client: connect, Hello and reconnect with back-off, non-blocking on the
  event loop.
* Tests from the Go version's sample messages, and a fake MOAS server in
  `tests/` that later stages reuse.  Fuzz targets for the framer and decoder,
  with the `fuzz` preset, a replay program that runs the stored inputs as
  tests where libFuzzer is not available (Windows), and fuzzing runs on
  FreeBSD.
* `moasweb -watch`, with a first version of `app/main.c` and `app/args.c`:
  connects as the given station and radio and prints everything the server
  sends, including TR changes.  It is read-only: it sends only Hello, never
  a frequency (which can switch relays), an antenna selection or a function.
  A radio whose frequency the server does not know yet gets no
  configuration, and watch mode says so.  It stays in moasweb as a
  troubleshooting aid.  First milestone: it runs against a `--sim` server
  and the real switch.

The Go version's separate `moasctl` tool is not carried over; its read-only
part lives on as `-watch`.

As built, Stage 2 differs from the plan, or adds to it, in these ways:

* A message that cannot be decoded is logged and skipped, and the
  connection carries on.  Only a stream that can no longer be split into
  messages (text that is not an object, unbalanced brackets, a message over
  `CAPS_OASP_MESSAGE`) drops the connection.  The Go version dropped it for
  any malformed JSON.
* Reconnecting starts at the Go version's 2 seconds and doubles up to 30
  seconds while the server stays unreachable, returning to 2 seconds once
  it answers: gentler on a server that has trouble with repeated
  connections.
* Text from the server is cleaned when decoded: control characters, C1
  controls and invalid UTF-8 become `?`, so nothing printed or shown later
  can carry terminal escape codes (`str_copy_clean()`).
* The platform layer gained `proc.h`, so that on Windows the command-line
  arguments are UTF-8 and the console shows UTF-8, and `net_pair.c`, a pair
  of loopback sockets that wakes the event loop on Ctrl+C.  A second Ctrl+C
  stops the program at once, in case shutting down hangs.
* `app/` is a library (`moas_app`) with only `main()` outside it, so that
  tests can run watch mode against the fake server.  A test checks that
  watch mode sends nothing but Hello.
* The fuzz targets replay their stored corpus as ordinary tests on every
  system; real fuzzing (`scripts/fuzz.sh`, the `fuzz` preset) runs on
  FreeBSD and Linux.
* Checked against MOAS Server v2.0.4 in `--sim` mode on Windows: watch mode
  connected, explained the missing configuration, then showed the radio's
  buttons and states once another client reported a frequency.

## Stage 3: frequency sources

* `freq/source.h`: the operations every source type provides, and the
  registry that maps a type name such as `"n1mm"` to them.
* `freq/tracker`: per-radio priority and failover, with the Go version's
  rules.
* `freq/n1mm`: UDP listener and a strict reader for the XML tags used.
  Fuzzed.
* `freq/rigctld`: poller and reply parser.  Fuzzed.
* `docs/adding-a-frequency-source.md`, using rigctld as the example.
* Tests ported from the Go `freq` tests, on loopback sockets.

As built, Stage 3 differs from the plan, or adds to it, in these ways:

* Each source type reads its own settings straight from the JSON of its
  entry in a radio's `"Sources"`, so a new type can have settings of its
  own without changing any shared struct.  Unknown settings are refused,
  and the error messages follow the Go version's ("PollMilliseconds does
  not apply to n1mm").  Addresses must be numeric, as everywhere else.
* The manual override (a typed frequency holds until the radio is tuned)
  stays in the app, as in the Go version, where it is part of passing
  frequencies to the server; it comes with Stage 5.  The tracker itself
  follows the Go `freq.Tracker` exactly.
* The tracker checks for stale readings itself, four times a second, on
  the event loop; tests replace its clock.
* N1MM's "radios heard" list and rigctld's one-off Test, both for the
  settings page, are here already.
* N1MM station names longer than CAPS_N1MM_STATION are cut short, and
  readings must be between 1 kHz and 1 THz.
* Not yet tried against a real N1MM or rigctld: nothing in moasweb uses
  the sources until Stage 5 wires them up.

## Stage 4: HTTP server

* Request parsing through picohttpparser, with our own checks on top:
  header and body caps, only the methods and headers we use.  Fuzzed.
* A route table of paths to handler functions.
* JSON replies and server-sent events, with a limit on clients and time
  limits for slow or stuck connections.
* `Host` and `Origin` checked on every request; settings changes accepted
  only from the local computer.
* Tests: parser edge cases and end-to-end requests over loopback.

As built, Stage 4 differs from the plan, or adds to it, in these ways:

* Responses and streams live in `server.c` with the connections, rather
  than in files of their own; `http.h` is the one header the app uses.
* A handler can answer later (the settings page's rigctld Test waits for
  rigctld).  If the browser goes first, the handler's cancel callback runs,
  so it can stop what it started.  Cancel callbacks, like handlers, only run
  from the event loop, never inside a call the app makes.
* The guard follows the Go version: when the page is served on a loopback
  address only, the `Host` must be a loopback name (against DNS
  rebinding); a POST must come from the page's own origin, if the browser
  says, and be `application/json`; local-only routes refuse other
  computers.
* Every response says `Cache-Control: no-store`, `X-Content-Type-Options:
  nosniff`, `X-Frame-Options: DENY` and `Referrer-Policy: no-referrer`, and
  carries a strict `Content-Security-Policy` unless the handler gives its
  own (the page will, in Stage 5).
* Refused outright: chunked request bodies (`Transfer-Encoding`), more than
  one `Content-Length` or `Host`, targets that are not plain paths, folded
  header lines, control characters in headers, and HTTP versions other than
  1.0 and 1.1.
* Limits: 10 s to send a request's head and body, 30 s idle between
  requests, 30 s without progress while sending, 32 connections, 16
  live-update streams, and 1 MiB waiting for any one browser before it is
  dropped.

## Stage 5: moasweb

* Settings: the Go version's JSON format, `"Version"` and `"_comment"`
  handling, lookup order, per-field validation errors, saving in one step,
  refusing read-only files, and starting on defaults when the file is
  missing or broken.
* `page.html` embedded by CMake and unchanged; the web addresses and JSON
  replies match the Go version.
* Wiring: server link, tracker, and settings applied live on Save.
* Tests: the scenarios of the Go `web_test`, `settings_test` and
  `follow_test`.

## Stage 6: parity, packaging, 1.0

* Run alongside the Go version against a `--sim` server and the real switch,
  using a checklist of every Go feature.
* CPack packages (`.zip` for Windows, `.tar.gz` elsewhere), with checksums.
* Build and test on macOS, and with MSVC, before 1.0.
* User guide, `docs/architecture.md` and protocol notes.
* Release v1.0.0 and archive the Go repository with a pointer here.

## Later

Rotators; more frequency sources (flrig, OmniRig, a serial CAT port); a MOAS
server built on `oasp/`; small hardware.

## Testing machines

This Windows PC (MSYS2 CLANG64 toolchain), the maintainer's FreeBSD
machines, which also run fuzzing and the sanitizers, and a Linux virtual
machine (Debian 13).

**Deferred:** Stages 0 to 3 passed the check on Windows and FreeBSD, and
fuzzing on FreeBSD, but have not yet been checked on Linux (maintainer's
decisions, 2026-10-24, to keep going while the Debian VM is set up).  gcc
builds on Windows (part of `check.bat`) and on FreeBSD cover gcc's
warnings in the meantime.  What only Linux checks is memory leaks:
LeakSanitizer runs by default only there.  Run `scripts/check.sh` on Linux
before Stage 5 starts, and fix anything it finds then.

The maintainer keeps the git repository locally and decides later where, if
anywhere, it is published.
