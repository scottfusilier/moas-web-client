# Changelog

All notable changes are listed here.  The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and version
numbers follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

* Project foundations: CMake build with presets, compiler warnings and
  hardening, sanitizer builds, style configuration, a test harness, and a
  check script for Windows, FreeBSD and Linux.
* Vendored cJSON v1.7.19 and picohttpparser (commit `465a7ff`).
* Platform layer for Unix and Windows: error codes, network addresses,
  non-blocking TCP and UDP sockets, poll, files (capped reads, replacing a
  file in one step, removing files, making folders, the settings folder,
  UTF-8 paths on Windows) and clocks.
* Core layer: slices with range-checked number parsing, a bounded buffer,
  checked size arithmetic, logging that neutralises control characters, and
  the event loop with sockets and timers.
* The `banned_functions` test also keeps the C library's file and
  environment functions inside the platform layer.
* The MOAS protocol library (`src/oasp/`): splitting the server's stream
  into messages, decoding and encoding every message the client needs, and
  a client that connects, sends Hello and reconnects with back-off.  Text
  from the server is cleaned of control characters and invalid UTF-8.
* `moasweb -watch`: prints what the MOAS server reports for one radio,
  sending nothing but Hello.  `-help` and `-version`.
* Ctrl+C and SIGTERM stop the program cleanly; on Windows the command line
  and console use UTF-8.
* Fuzz targets for the framer and the decoder, the `fuzz` preset and
  `scripts/fuzz.sh`; their corpus also runs as tests everywhere.
* Frequency sources (`src/freq/`): a tracker per radio with the Go
  version's priority and failover rules; the `n1mm` type (shared UDP
  listeners, a strict RadioInfo reader, the radios heard) and the `rigctld`
  type (polling, and a one-off Test); a registry so that a new type is one
  file and one line.  Fuzz targets for both readers, and
  `docs/adding-a-frequency-source.md`.
