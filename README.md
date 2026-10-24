# moas-web-client

A cross-platform client for the MOAS II antenna switch server by M1DST and
K1XM.  It connects to a MOAS server over the network and shows each radio's
antenna buttons in a web page.  Each radio's frequency comes from N1MM
Logger+, hamlib's rigctld, or is typed on the page.  It is meant to run on
Windows, Linux, FreeBSD and macOS; for now it is built and tested on Windows,
FreeBSD and Linux.

moas-web-client is an independent project and is not affiliated with or
endorsed by the authors of MOAS.

## Status

Under development: the web page does not exist yet.  This is a rewrite in C
of an earlier version written in Go; the plan and its progress are in
[docs/ROADMAP.md](docs/ROADMAP.md).

What works so far is watch mode, for troubleshooting:

```
moasweb -watch -server 192.168.1.10:12059 -station 1 -radio 1
```

It connects to the MOAS server as that radio and prints everything the
server reports (the buttons, what is selected, and transmit/receive
changes) until you press Ctrl+C.  It sends nothing but its greeting, so it
never changes what the switch does.  `moasweb -help` lists the options.

## Building

Needs CMake 3.21 or later, Ninja, and a C11 compiler: clang on FreeBSD, gcc
or clang on Linux, and the MSYS2 CLANG64 toolchain on Windows.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The program is `build/debug/src/app/moasweb`.  [CONTRIBUTING.md](CONTRIBUTING.md)
has the details for each system, the other presets, and how to work on the
code.

## License

BSD 2-Clause; see [LICENSE](LICENSE).  The libraries in `third_party/` keep
their own licenses; see [third_party/README.md](third_party/README.md).
