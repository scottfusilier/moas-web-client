# moas-web-client roadmap

This is the plan for moas-web-client, a web client for the MOAS antenna
switch server, written in C.  The project and the program it builds are
both called `moas-web-client`.

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
* No hosted services in use.  Checks run from a script
  (`scripts/check.sh`, `scripts\check.bat`) on the maintainer's machines,
  not in hosted CI.
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
                 app/  (moas-web-client)
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
├── moas-web-client.example.json   an example settings file with every setting
├── .editorconfig, .clang-format, .gitattributes, .gitignore
├── cmake/   platform  warnings  hardening  sanitizers  strip  format  check_banned  embed  package (.cmake)
├── scripts/ check.sh, check.bat   build and test every preset, check formatting
│            fuzz.sh               build with libFuzzer and fuzz each target (Unix)
├── src/
│   ├── platform/                  prefix plat_; the only #ifdef _WIN32
│   │   ├── err.h, err.c                           error codes shared by every layer
│   │   ├── net.h, net_addr.c, net_posix.c, net_win.c   addresses, sockets, poll
│   │   ├── net_pair.c                             a connected pair of loopback sockets
│   │   ├── fs.h, fs_posix.c, fs_win.c             capped reads, replace in one step, settings
│   │   │                                          folder, program folder, read-only files
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
│   │   ├── sha256.h, sha256.c     SHA-256: settings file versions, the page's script hashes
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
│   │   ├── rigctld.c, rigctld_reply.c   poller and Test; reply parser (fuzzed)
│   │   └── n3fjp.c, n3fjp_reply.c       poller and Test; line reader (fuzzed)
│   ├── http/                      prefix http_
│   │   ├── http.h                 the interface: routes, replies, streams, server
│   │   ├── parse.h, parse.c       request heads: picohttpparser plus our checks (fuzzed)
│   │   ├── router.c, internal.h   method + path to handler; per-route body limit, local only
│   │   ├── guard.c                Host, Origin, Content-Type and local-only checks
│   │   └── server.c               connections, responses with security headers,
│   │                              replies sent later, server-sent events, limits
│   └── app/                       prefix app_; moas-web-client
│       ├── main.c                 the command line, then web mode or watch mode
│       ├── args.h, args.c         command line; which options were given
│       ├── serve.h, serve.c       web mode: startup settings, start, Ctrl+C, shutdown
│       ├── settings.h, settings.c       settings, defaults, validation by field
│       ├── settings_file.h, settings_file.c   lookup order, load, version, save in one step
│       ├── link.h, link.c         server connection as the page sees it; the snapshot JSON
│       ├── runner.h, runner.c     sources, the follower, applying saved settings live
│       ├── web.h, web.c           route handlers, live updates, the page's CSP
│       ├── watch.c                -watch: prints what the server sends, changes nothing
│       └── page.h, page.html      embedded at build time by cmake/embed.cmake
├── tests/
│   ├── test.h                     harness macros
│   ├── fake_moas_server.c/.h      scripted MOAS server on loopback, same event loop
│   ├── oasp_samples.h             sample server messages shared by tests and corpus
│   ├── fake_rig.c/.h              rigctld stand-in on loopback, same event loop
│   ├── fake_n3fjp.c/.h            N3FJP API stand-in on loopback, same event loop
│   ├── http_client.c/.h           a small HTTP client on the same event loop
│   └── test_<module>.c            one per module; test_web.c runs moas-web-client end to end
├── fuzz/
│   ├── fuzz.h                     the entry point every target defines
│   ├── fuzz_frame.c  fuzz_message.c  fuzz_n1mm_xml.c  fuzz_rigctld_reply.c
│   │   fuzz_n3fjp_reply.c  fuzz_http_parse.c  fuzz_settings.c
│   ├── replay_main.c              runs stored inputs as tests where libFuzzer is unavailable
│   └── corpus/<target>/
├── third_party/cjson/, third_party/picohttpparser/   each with its LICENSE; versions,
│                                  hashes and reasons in third_party/README.md
└── docs/    ROADMAP.md  user-guide.md  architecture.md  adding-a-frequency-source.md
```

Parsing of outside input is always in its own file with no I/O, so that each
parser can be fuzzed alone and the risky code is easy to find.

The web routes, which `page.html` uses: `GET /`, `GET /events`,
`GET /api/state`, `POST /api/frequency`, `POST /api/select`,
`POST /api/function`, `POST /api/step`, `POST /api/rx-same-as-tx`, and,
local only, `GET` and `POST /api/settings`,
`GET /api/n1mm-heard`, `POST /api/rigctld-test` and
`POST /api/n3fjp-test`.

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
  `~/Library/Application Support/moas-web-client/`.
* Frequency source types are added by writing one file and one registry line;
  each type checks its own settings.

## How a stage is done

A stage is done when the check script passes on Windows, FreeBSD and Linux:
every preset builds with warnings as errors, every test passes, including
under the sanitizers, and the formatting is clean.  Fuzz targets are run on
FreeBSD.  The maintainer runs the FreeBSD and Linux checks.

Each stage comes with a short summary: what is in it, where to start
reading, and anything uncertain.  It is reviewed and committed before the
next stage starts.  A stage is roughly 500 to 1,500
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
otherwise macOS is tested after 1.0, and any fixes stay in `platform/`.

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
* Tests from sample server messages, and a fake MOAS server in
  `tests/` that later stages reuse.  Fuzz targets for the framer and decoder,
  with the `fuzz` preset, a replay program that runs the stored inputs as
  tests where libFuzzer is not available (Windows), and fuzzing runs on
  FreeBSD.
* `moas-web-client -watch`, with a first version of `app/main.c` and
  `app/args.c`: connects as the given station and radio and prints
  everything the server sends, including TR changes.  It is read-only: it
  sends only Hello, never a frequency (which can switch relays), an
  antenna selection or a function.  A radio whose frequency the server
  does not know yet gets no configuration, and watch mode says so.  It
  stays in moas-web-client as a troubleshooting aid.  First milestone: it
  runs against a `--sim` server and the real switch.

As built, Stage 2 differs from the plan, or adds to it, in these ways:

* A message that cannot be decoded is logged and skipped, and the
  connection carries on.  Only a stream that can no longer be split into
  messages (text that is not an object, unbalanced brackets, a message over
  `CAPS_OASP_MESSAGE`) drops the connection.
* Reconnecting starts at 2 seconds and doubles up to 30
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
* `freq/tracker`: per-radio priority and failover.
* `freq/n1mm`: UDP listener and a strict reader for the XML tags used.
  Fuzzed.
* `freq/rigctld`: poller and reply parser.  Fuzzed.
* `docs/adding-a-frequency-source.md`, using rigctld as the example.
* Tests on loopback sockets.

As built, Stage 3 differs from the plan, or adds to it, in these ways:

* Each source type reads its own settings straight from the JSON of its
  entry in a radio's `"Sources"`, so a new type can have settings of its
  own without changing any shared struct.  Unknown settings are refused,
  and the error messages name the setting ("PollMilliseconds does not
  apply to n1mm").  Addresses must be numeric, as everywhere else.
* The manual override (a typed frequency holds until the radio is tuned)
  stays in the app, where it is part of passing frequencies to the
  server; it comes with Stage 5.
* The tracker checks for stale readings itself, four times a second, on
  the event loop; tests replace its clock.
* N1MM's "radios heard" list and rigctld's one-off Test, both for the
  settings page, are here already.
* N1MM station names longer than CAPS_N1MM_STATION are cut short, and
  readings must be between 1 kHz and 1 THz.
* Not yet tried against a real N1MM or rigctld: nothing in moas-web-client uses
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
* The guard: when the page is served on a loopback
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

## Stage 5: moas-web-client

* Settings: a JSON format with `"Version"` and `"_comment"` handling,
  lookup order, per-field validation errors, saving in one step, refusing
  read-only files, and starting on defaults when the file is missing or
  broken.
* `page.html` embedded by CMake.
* Wiring: server link, tracker, and settings applied live on Save.
* Tests: the page driving the server, refused requests, reconnecting, live
  updates, the follower, and the settings file.

As built, Stage 5 differs from the plan, or adds to it, in these ways:

* The settings file is the `-config` file if given; else
  `moas-web-client.json` beside the program, if there is one; else
  `moas-web-client/moas-web-client.json` in the settings folder.  On Unix
  "beside the program" works only when it was started by a path, not found
  on the `PATH`.
* A file's version is the SHA-256 of its contents.  The page sends back the
  version it was shown; if the file has changed since, the save is refused
  (409) rather than overwriting someone's hand edit.
* Saving refuses a file that is read-only for this user, with a message
  saying so, even on Unix where the folder would allow replacing it.
* A file with a mistake never stops moas-web-client starting: it prints the
  problem, runs on the defaults and leaves the file alone.  The page's
  settings section shows as much of the file as could be read, with the
  mistake marked.
* Command-line options win over the file, and the page lists which were
  given.  `-radios` cannot be combined with a file that lists radios.
  Watch mode takes the server and station from the settings file unless
  they are given.
* Saving applies everything at once except `Listen`: the link reconnects
  if the server, station, radios or grid size changed (keeping each
  remaining radio's frequency), and the sources restart only if they
  changed.
  The reply says when a restart is needed for `Listen`.  The last six
  servers are remembered for the page to offer.
* The snapshot lives in `link.c`, which owns the state it describes.
  Changes are sent to the open pages on the loop's next pass, so a burst of
  messages from the server makes one update.
* The page's `Content-Security-Policy` allows only its own inline script
  and style, by SHA-256 hash, computed at startup from the embedded page.
* Tests run the program end to end on loopback ports chosen by the
  system: the page's requests go through the runner and the link to a fake
  MOAS server, and the follower is tested with real N1MM (UDP) and rigctld
  (TCP) sources.  `fuzz_settings` checks that any accepted file reads back the
  same after being written, and that a refused one changes nothing.
* Still to be tried by hand, as part of Stage 6's checklist: the page in
  browsers, against the real switch, and with a real N1MM and rigctld.
* No test talks to a real MOAS server.  Manual tries use a `--sim` server
  on a port other than 12059, so that a real server on this computer is
  never reached by mistake.

## Stage 6: DXLog, every configuration, N3FJP, trial, packaging, 1.0

* DXLog as a frequency source.  DXLog.net, like N1MM Logger+, reports each
  radio's frequency over UDP.  First find out exactly what it sends, from
  packets captured at a station: if it is N1MM's RadioInfo format, the
  `n1mm` type may already read it (its reader does not check which program
  sent the packet) and needs only tests and documenting; otherwise it gets
  a `dxlog` type of its own, with a fuzzed reader, following
  `docs/adding-a-frequency-source.md`.  The captured packets become test
  samples either way.
  * As built: DXLog.net 2.6 sends N1MM's RadioInfo layout, with
    `<app>DXLog</app>`, `<Station>` for the station name, no
    `IsConnected`, and a packet per radio about once a second.  It is a
    `dxlog` type beside `n1mm`, in the same file and with the same
    settings and reader; sources of both types on one address share its
    listener, and each follows only its own program's packets (a packet
    without `<app>` counts as N1MM's).  The page offers "+ DXLog" and
    shows each source only its own logger's radios heard.
  * Since DXLog sends no `IsConnected`, a radio set up in DXLog with no
    rig behind it keeps reporting the frequency last shown for it.
  * Tried by hand: a live DXLog with radio 1 connected, tuning across
    bands, through moas-web-client to a `--sim` server; the buttons
    followed each band change.
* Every MOAS configuration, not only the ones tried so far.  Antenna
  selection must work whatever a server's configuration uses, so the
  client handles the whole of the protocol for it (rotators excepted, see
  Later):
  * Text from the server: shown on the page and in the program's output.
    One action asks the client to stop, as when a server that allows only
    one client refuses another; then say so, and retry only slowly
    rather than reconnecting every few seconds.
  * On/off functions: a function that is not momentary is turned off by
    sending "deselect" when it is on.  Today every press sends "select", which is
    right only for momentary functions.
  * Additive antenna systems: antennas marked `Additive` in a
    Configuration are added to and removed from the selection ("additive
    select" and "additive deselect") rather than replacing it.
  * Relative antennas: next and previous antenna in the band's relative
    list, as page buttons; and, behind a setting, following a logger's
    antenna changes (N1MM's and DXLog's antenna number) the same way.
  * Switch not reachable: TR 0 shown as "unavailable", not blank.
  * RX the same as TX, as an antenna change.
  * Logger messages both ways: a logger's F-key text sent for a radio,
    and shown when the server passes one on from another client.
  * Not needed: asking the server for its list of layouts, and the
    callsign, direction and distance a Radio message may carry; nothing
    in a MOAS configuration depends on them.
  * Each with tests against the fake MOAS server, and a fake server
    configuration that uses it.
  * As built: all of the above.  A click picks its request from what the
    server said about the button: an additive antenna is added when
    available and taken away when selected (receive following transmit
    where it can); any other antenna replaces the selection in its
    system; a function that is not momentary is turned off when it is on
    (state 2).
    The page lights a function when it is on and disables it when the
    server says unavailable (state 0); before, it lit them when merely
    available.  Prev / Next and RX = TX are buttons on each radio; RX =
    TX asks for the transmit antenna exclusively with receive the same.
  * A refusal (Text asking the client to stop) stays on the page, and the
    next attempt waits a minute instead of the usual 2 to 30 seconds.
  * Following a logger's antenna changes is a source setting,
    `FollowAntenna`, off unless turned on, since it switches relays with
    no click on the page.  A logger may pick another antenna itself on a
    band change, which the server already handles from the frequency, so
    a change is passed on only while the frequency is inside the band the
    server last sent buttons for.  Only the source a radio is following
    may change its antenna (the tracker passes on events from that source
    only).  `SendFKeyText` (N1MM only, also off unless turned on) sends
    N1MM's F-key caption as a Logger message whenever it changes.
  * Tried by hand against a `--sim` server: Prev and Next step through the
    configuration's relative antenna lists.
* Transmit/receive state when the switch does not report it.  A server
  starts every radio as inhibited, and only a switch with `IndicateTR` in
  the server's configuration ever changes that; without it, every radio
  shows INHIBITED for good, which looks like a fault.
  * The INHIBITED badge's tooltip says what it means, and that if it never
    changes the switch needs `IndicateTR`; the README says the same under
    troubleshooting.
  * Until some radio on the connection has reported receive or transmit,
    an inhibited radio is shown as TX/RX NOT REPORTED, quietly, with the
    same advice.  The usual badges return as soon as one does.  (A
    station that inhibits a radio from the start with a function sees
    "not reported" until something is keyed.)
  * A `ShowTXRX` setting, on unless turned off: off, the client does not
    ask the server for transmit/receive state at all, and shows none.
* N3FJP as a frequency source, for Amateur Contact Log and the other N3FJP
  programs, through their TCP API.
  * As built: an `n3fjp` type of its own, with rigctld's shape: it
    connects, sends `<CMD><READBMF></CMD>` every `PollMilliseconds`, and
    has the same reply, connect and retry times, settings checks and page
    Test.  Changes normally arrive unasked, as soon as N3FJP sees them; the
    poll (default 2000, at least 1000) checks that N3FJP still answers and
    catches a change that was missed.  A capture polling every 250 ms
    found every change first in a message N3FJP sent unasked, and ACLog
    updating its frequency only every 4 seconds while tuning, so faster
    polling would find nothing new; it would also cost the other programs,
    since N3FJP repeats every reply to every program connected to it.  A
    reading is renewed only by the poll between changes, so
    `TimeoutSeconds` defaults to, and must be at least, the poll plus 3
    seconds (5 by default); it follows a changed poll unless set, and is
    saved only if set.  Only `FREQ` is read (MHz, to
    1 Hz, turned into whole kHz); band and mode are not used, since ACLog
    sends the old band with the new frequency for up to 2 seconds after a
    band change.  A reading sent without being asked (a change, or a
    copy of a reply to another program) counts like a reply; other
    commands and blank lines are ignored, and a line too long to keep
    is dropped rather than failing the source.  An empty or zero
    frequency fails the source with "no usable frequency".
  * Found from traffic captured with Amateur Contact Log: lines end with
    CR+LF and may come several at once; changes are sent unasked, on a
    2-second timer that alternates, it seems, between reading the rig's
    frequency and its mode; more than one program may connect; on exit it
    sends two blank lines and closes.  The captures are the fuzz corpus.
  * The page offers "+ N3FJP"; `POST /api/n3fjp-test` reads once.
  * N3FJP reports the frequency of the rig in use, so a source follows
    whichever rig that is.  Not yet tried: a rig swap (Ctrl+X).
* Tried by hand instead of a formal checklist: everyday use by the
  maintainer, TX/RX on a real switch, and additive antennas and on/off
  functions against a `--sim` server using the sample Tribander Stack
  configuration.  Covered only by the automated tests at 1.0: a refusal
  (`OneClient`), rigctld, N1MM's `FollowAntenna` and F-key text, and an
  N3FJP rig swap.
* The double-click on a function was removed.  A server marks on/off
  functions `Alternate`, but most ignore the "alternate" request and a
  few act on it in ways that surprise; a click already turns them on and
  off.
* CPack packages (`.zip` for Windows, `.tar.gz` elsewhere), with checksums.
  As built: `cmake/package.cmake`, the `package` target of the `release`
  preset; each is built on the system it is for.
* `docs/user-guide.md` and `docs/architecture.md`.  Separate protocol notes
  were dropped; only contributors would read them.
* Released as v1.0.0 on 2026-10-31.

## Later

macOS and MSVC builds; rotators; more frequency sources (flrig, OmniRig, a serial CAT port); a MOAS
server built on `oasp/`; small hardware.

## Testing machines

This Windows PC (MSYS2 CLANG64 toolchain), the maintainer's FreeBSD
machines, which also run fuzzing and the sanitizers, and a Linux machine
(Ubuntu 24.04), the only one where LeakSanitizer runs.

Linux was first checked on 2026-10-29, after Stage 5 and part of Stage 6:
`scripts/check.sh` with gcc and with clang, and `scripts/fuzz.sh`, all
pass, with no leaks.  That run found a race in a test (`test_web` read the
page's state before the server's selection had arrived), now fixed.
