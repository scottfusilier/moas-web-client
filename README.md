# moas-web-client

A cross-platform client for the MOAS II antenna switch server by M1DST and
K1XM.  It connects to a MOAS server over the network and shows each radio's
antenna buttons in a web page.  Each radio's frequency comes from N1MM
Logger+, DXLog.net, hamlib's rigctld, an N3FJP program such as Amateur
Contact Log, or is typed on the page.  It is meant to run on Windows,
Linux, FreeBSD and macOS; for now it is built and tested on Windows,
FreeBSD and Linux.

moas-web-client is an independent project and is not affiliated with or
endorsed by the authors of MOAS.

## Status

Version 1.0.0.  [CHANGELOG.md](CHANGELOG.md) lists what is in it, and
[docs/ROADMAP.md](docs/ROADMAP.md) has what is planned next.

## Using it

Run `moas-web-client` (no options are needed) and open the address it
prints, `http://127.0.0.1:8080/`, in a browser.  On the first run the page
opens its settings: enter your MOAS server's address and your radios, and
save.  The page then shows each radio's antenna buttons as the MOAS server
lays them out; click one to select it.  Press Ctrl+C to stop.

The [user guide](docs/user-guide.md) covers the page, setting up each
frequency source, every setting, using the page from other computers, and
troubleshooting.

By default the page can be opened only on the computer moas-web-client
runs on.  It can be served to the network, but the MOAS protocol has no
login, so anyone who can open the page can switch antennas.

## Building

Needs CMake 3.21 or later, Ninja, and a C11 compiler: clang on FreeBSD, gcc
or clang on Linux, and the MSYS2 CLANG64 toolchain on Windows.

```
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The program is `build/release/src/app/moas-web-client`
(`moas-web-client.exe` on Windows).
[CONTRIBUTING.md](CONTRIBUTING.md#building) has step-by-step instructions
for each system, the other presets, and how to work on the code, and
[docs/architecture.md](docs/architecture.md) describes how the program is
put together.

## License

BSD 2-Clause; see [LICENSE](LICENSE).  The libraries in `third_party/` keep
their own licenses; see [third_party/README.md](third_party/README.md).
