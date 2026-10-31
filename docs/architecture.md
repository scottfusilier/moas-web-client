# Architecture

moas-web-client is one program with one thread and one event loop.  It holds
a connection to a MOAS server, follows each radio's frequency from its
sources, and serves a page that shows the server's buttons and sends clicks
back.

```
 N1MM, DXLog ──UDP──▶ ┌─────────┐              ┌──────┐
 rigctld, N3FJP ─TCP─▶│  freq/  │──frequency──▶│      │──TCP──▶ MOAS server
                      └─────────┘              │ app/ │◀──────
                      ┌─────────┐   clicks     │      │
 browser ◀──HTTP────▶ │  http/  │◀────────────▶│      │
                      └─────────┘   snapshot   └──────┘
```

## Layers

Each directory under `src/` is a static library, linked only against the
layers below it, so the build enforces the layering.

| Layer | Holds |
|-------|-------|
| `app/` | The program: command line, settings, and the wiring of the layers below |
| `oasp/` | The MOAS protocol: splitting the stream, messages, connecting and reconnecting |
| `freq/` | Frequency source types, and the tracker that chooses between them |
| `http/` | The web server: requests, routes, guards, responses, server-sent events |
| `core/` | Slices, bounded buffers, limits, logging, JSON access, the event loop |
| `platform/` | Everything specific to a system: sockets, files, clocks, signals |

`oasp/`, `freq/` and `http/` do not know about each other; only `app/`
connects them.  `oasp/` has no dependency on the web page, so it can be used
on its own.

## The event loop

Everything runs on the loop in `core/loop.c`, which watches sockets and runs
timers and deferred calls.  Nothing may block: sockets are non-blocking, and
long waits are timers.  This is why host names are not looked up, since a
lookup blocks.  With one thread there are no locks, and a callback sees a
consistent state, but a callback must not destroy what is currently calling
it.  Ctrl+C and SIGTERM wake the loop through a pair of loopback sockets, so
shutting down is an ordinary event too.

## The parts of `app/`

| File | Does |
|------|------|
| `main.c`, `args.c` | Reads the command line, then starts web mode or watch mode |
| `serve.c` | Web mode: startup settings, creating everything, shutdown |
| `settings.c`, `settings_file.c` | Settings, defaults and checks by field; finding, loading and saving the file |
| `link.c` | The server connection as the page sees it; turns clicks into requests and builds the snapshot |
| `runner.c` | Owns the link and the sources; passes frequencies to the link; applies saved settings |
| `web.c`, `page.html` | The routes, live updates and the page |
| `watch.c` | Watch mode: prints what the server sends and sends only its greeting |

## How things flow

**A frequency.**  A source reports through its feed to the radio's tracker
(`freq/tracker.c`), which follows the first source that is up.  When the
followed source or its reading changes, the runner sends the frequency to the
link, and so to the server.  A frequency typed on the page goes to the link
directly and holds until the next such change, so the last change wins.

**A click.**  The page posts to a route such as `/api/select`.  `link.c` picks
the request from what the server last said about that button, such as adding
an additive antenna or turning off a function that is on, and sends it.  The
page changes nothing itself: it waits for the server's reply to come back as
a new snapshot.

**An update.**  Messages from the server update the state in `link.c`, and
sources update the trackers.  The page's update is sent on the loop's next
pass, so a burst of messages makes one update.  The update is a JSON snapshot
of everything the page shows, sent to each open page as a server-sent event.

**Saving settings.**  The page posts the whole settings object and the version
of the file it was shown, which is the SHA-256 of its contents.  If the file
has changed, the save is refused.  Otherwise the file is replaced in one step
and the settings are applied: the link reconnects if the connection settings
changed, and the sources restart only if they changed.

## Trust boundaries

All outside input is untrusted: the server's messages, logger packets, replies
from rigctld and N3FJP, requests from browsers, and the settings file.

* Each format has a parser in its own file with no I/O and hard limits, and
  each parser has a fuzz target.
* Input stays in slices until it is checked, and text is cleaned of control
  characters and invalid UTF-8 before it is kept or shown.
* Every limit is in `core/caps.h`.
* The page builds its content with `textContent`, and its
  `Content-Security-Policy` allows only its own script and style, by hash.

The web server's guards, in `http/guard.c`, check every request.  When the page
is served on a loopback address only, `Host` must be a loopback name, which
stops DNS rebinding.  A POST must be `application/json` and come from the page's
own origin if the browser names one, which stops other sites from sending
clicks.  Settings routes refuse other computers.

The MOAS protocol has no login, and neither does the page.  The default of
serving only this computer, and the warning printed otherwise, are the
protection for antenna switching.

## Adding to it

[adding-a-frequency-source.md](adding-a-frequency-source.md) shows how to add a
source type: one file and one registry line.  [CONTRIBUTING](../CONTRIBUTING.md)
has the code style and rules, and [ROADMAP](ROADMAP.md) has the decisions behind
the design.
