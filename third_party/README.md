# Third-party code

Each library here is a copy of an upstream release, unchanged, kept at a
pinned version.  Nothing is fetched at build time.

A new dependency must have a license compatible with BSD-2-Clause (MIT, BSD
or ISC; no GPL, and LGPL avoided), be small enough to vendor and read, be
actively maintained and widely used, and build cleanly on gcc, clang and
MSVC.  Record it below with the reason it is here.

## cJSON

* Why: reading and writing JSON, for the MOAS protocol and the settings
  file.
* Upstream: <https://github.com/DaveGamble/cJSON>
* Version: v1.7.19 (released 2025-09-09)
* License: MIT (`cjson/LICENSE`)
* Files: `cJSON.c`, `cJSON.h`, `LICENSE`, from the `v1.7.19` tag.
* SHA-256:
  * `cJSON.c` `298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d`
  * `cJSON.h` `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee`
  * `LICENSE` `a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c`
* Local changes: none.

## picohttpparser

* Why: parsing HTTP requests from browsers.  It parses only; the server,
  routing and server-sent events are our own.
* Upstream: <https://github.com/h2o/picohttpparser>
* Version: commit `465a7ff09fbd3432fe56c673451f5460154d1f07` (2026-09-29).
  The project does not make releases.
* License: MIT or the Perl license, at our choice; we use MIT.  Upstream has
  no separate license file; `picohttpparser/LICENSE` is the notice from the
  top of `picohttpparser.h`, copied unchanged.
* Files: `picohttpparser.c`, `picohttpparser.h`, from that commit.
* SHA-256:
  * `picohttpparser.c` `6480689711996bc9a219e26df27ac32ebbb7dce2f24a63a316d041b08f646199`
  * `picohttpparser.h` `1fc9074dd12418b2b91e55ef3a8279bccf8d3577b790dea6f2f908ab2a157f1c`
* Local changes: none.
* Note: on MSVC its header defines `ssize_t` as a macro, so only
  `src/http/parse.c` includes it.

## Updating a library

1. Download the new files from the upstream tag or commit.
2. Read the upstream changes since the pinned version.
3. Replace the files, update the version and SHA-256 values here, and note
   the update in `CHANGELOG.md`.
4. Build and run the tests, including the sanitizer and fuzzing jobs.
