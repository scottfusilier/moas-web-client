# moas-web-client

A cross-platform client for the MOAS II antenna switch server by M1DST and
K1XM.  It connects to a MOAS server over the network and shows each radio's
antenna buttons in a web page.  Each radio's frequency comes from N1MM
Logger+, DXLog.net, hamlib's rigctld, or is typed on the page.  It is meant
to run on Windows, Linux, FreeBSD and macOS; for now it is built and tested
on Windows, FreeBSD and Linux.

moas-web-client is an independent project and is not affiliated with or
endorsed by the authors of MOAS.

## Status

Under development, not yet released; the plan and its progress are in
[docs/ROADMAP.md](docs/ROADMAP.md).

## Using it

Run `moas-web-client` (no options are needed) and open the address it
prints, `http://127.0.0.1:8080/`, in a browser.  On the first run there is
no settings file yet: open the page's settings section, enter your MOAS
server's address and your radios, and save.  The page then shows each
radio's antenna buttons as the MOAS server lays them out; click one to
select it.  Press Ctrl+C to stop.

The settings are kept in `moas-web-client.json`: beside the program if
there is one there, otherwise in your settings folder
(`%APPDATA%\moas-web-client` on Windows, `~/.config/moas-web-client` on
FreeBSD and Linux).  Edit them on the page's settings section, or by hand;
[moas-web-client.example.json](moas-web-client.example.json) shows every
setting, including where each radio's frequency comes from (N1MM Logger+,
DXLog.net or rigctld).  Command-line options are optional and win over the
file, for example `moas-web-client -server 192.168.1.10:12059`;
`moas-web-client -help` lists them.

For DXLog.net, turn on Options → Broadcast → Radio information, and use a
`dxlog` source; it listens where DXLog sends by default, 127.0.0.1:12060.
N1MM and DXLog can both send to the same address: each source follows
only its own logger's packets.  DXLog does not say when a radio is not
connected, so a radio that is set up in DXLog but switched off keeps the
frequency DXLog last showed for it.

Two settings of an N1MM or DXLog source are off unless turned on, on the
settings section or in the file: `FollowAntenna` steps to the next antenna
in the band's relative list when the logger moves to its next antenna
(N1MM's Alt-F9), but not when that comes with a band change, since the
server picks for the new band itself; and `SendFKeyText` (N1MM only)
passes the caption of the F-key message being sent to the server, which
shows it to the other clients serving that radio.

By default the page can be opened only on the computer moas-web-client
runs on.  `-listen 0.0.0.0:8080` serves it to the network, but the MOAS
protocol has no login, so anyone who can open the page can switch
antennas.  Settings can only be changed from the computer moas-web-client
runs on.
For troubleshooting, watch mode prints everything the server reports for
one radio (the buttons, what is selected, and transmit/receive changes):

```
moas-web-client -watch -server 192.168.1.10:12059 -station 1 -radio 1
```

It sends nothing but its greeting, so it never changes what the switch does.

## Troubleshooting

**Every radio shows TX/RX NOT REPORTED, or INHIBITED that never
changes.**  A MOAS server starts every radio as inhibited, and only a
switch that reports transmit and receive changes that.  Add
`"IndicateTR" : true` to the switch's entry under `"Switches"` in the
server's configuration and restart the server.  To show no
transmit/receive state at all, turn off "Show transmit/receive state" in
the advanced settings (`"ShowTXRX" : false`).

**The page says the MOAS server refused this client.**  The server allows
only one client and another is connected; moas-web-client asks again every
minute.

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
for each system, the other presets, and how to work on the code.

## License

BSD 2-Clause; see [LICENSE](LICENSE).  The libraries in `third_party/` keep
their own licenses; see [third_party/README.md](third_party/README.md).
