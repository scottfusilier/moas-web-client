# moas-web-client user guide

moas-web-client connects to a MOAS II server and shows each radio's antenna
buttons in a web page.  It needs a MOAS server that is already set up and
running; it does not configure the server or the switch.

## Getting started

### 1. Install

Unpack the package anywhere you like.  There is nothing to install and
nothing else to download.  Each package comes with a `.sha256` file, which
you can compare with the result of:

| System  | Command |
|---------|---------|
| Windows | `certutil -hashfile moas-web-client-VERSION-windows-amd64.zip SHA256` |
| Linux   | `sha256sum moas-web-client-VERSION-linux-x86_64.tar.gz` |
| FreeBSD | `sha256 moas-web-client-VERSION-freebsd-amd64.tar.gz` |

The program is not signed, so Windows may warn about it the first time it
runs.

### 2. Run

Run `moas-web-client` (`moas-web-client.exe` on Windows) from a terminal.
It prints the settings file it is using and the address of the page:

```
Open http://127.0.0.1:8080/ in a browser.  Press Ctrl+C to stop.
```

The terminal has to stay open while you use the page.  Ctrl+C stops the
program; closing the browser does not.

### 3. Enter your settings

On the first run the page opens its settings section by itself.

1. Under **MOAS server**, enter the server's address, for example
   `192.168.1.10:12059`.  Addresses must be numeric, or `localhost`; host
   names are not looked up.
2. Under **Station**, enter this station's number in the server's
   configuration.
3. Under **Radios**, keep one entry for each radio you use, with its number
   in the server's configuration.
4. Add a frequency source for each radio (see
   [Frequency sources](#frequency-sources)), or none if you will type the
   frequency on the page.
5. Press **Save**.

The settings take effect at once.

### 4. Set a frequency

A MOAS server sends a radio's buttons only once it knows the radio's
frequency, so until then the page says "no band yet: set a frequency".  Tune
the radio if it has a source, or use the frequency box or a band button on
the page.

Reporting a frequency can make the server switch relays, whichever way the
frequency arrives.

## The page

Each radio has its own panel.

### Selecting antennas

The buttons are laid out as the server sends them.  A selected button shows
a **TX** or **RX** mark, and the line above the buttons names what is in use.

* **Click** selects the antenna for transmit and receive, or for receive
  only if the switch below the buttons is set to **RX only**.
* **Right-click** always selects for receive only.
* A button marked "receive only" can be used only for receive.
* A greyed-out button is not available now.

In an additive antenna system, a click adds the antenna to the selection,
and another click takes it away.  Hover over a button to see whether it is
additive.

The round buttons are functions.  A momentary one acts once for each click.
Any other turns on and off with a click, and is outlined when it is on.

**◀ Prev** and **Next ▶** step through the band's relative antenna list in
the server's configuration.  **RX = TX** makes receive use the transmit
antenna.

### Frequency

The **Frequency (kHz)** box and **Set** send a frequency, and the band
buttons send the bottom of a band.  A typed frequency holds until the radio
is tuned or the page starts following a different source: the last change
wins.

The line under the radio's name shows where the frequency comes from, then
each source:

| Shown | Meaning |
|-------|---------|
| A frequency, with a green dot | The source is working |
| waiting | No reading yet |
| no recent reading | Its last reading is older than its timeout |
| not available, with a red dot | It has failed; hover over it to see why |

### Transmit and receive

The badge at the top of a panel shows the radio's state:

| Badge | Meaning |
|-------|---------|
| RECEIVE | Receiving |
| TRANSMIT | Transmitting; the panel is also outlined |
| INHIBITED | The radio may not transmit now, usually because another is transmitting |
| TX/RX NOT REPORTED | No radio has reported transmit or receive yet |
| TX/RX UNKNOWN | The switch is not reachable, so the server cannot tell |

### Messages

A message from the server appears at the top of the page, as do lost
connections and refusals.  A line starting "Logger:" under a radio's
selection is a logger's message for that radio, passed on by the server.

## Frequency sources

A radio can have several sources, in priority order.  The first one working
is followed, and if a higher one comes back it is followed again.  Use the
**↑** and **↓** buttons to change the order.  A radio with no sources takes
its frequency only from the page.

| Source | Add with | Default address | Works by |
|--------|----------|-----------------|----------|
| N1MM Logger+ | + N1MM | `127.0.0.1:12060` | Listening for its UDP broadcasts |
| DXLog.net | + DXLog | `127.0.0.1:12060` | Listening for its UDP broadcasts |
| hamlib's rigctld | + rigctld | `127.0.0.1:4532` | Connecting and asking |
| N3FJP programs | + N3FJP | `127.0.0.1:1100` | Connecting to its TCP API |

### N1MM Logger+

In N1MM, open Config → Configure Ports, Mode Control, Winkey, etc → Broadcast
Data, turn on **Radio**, and send to `127.0.0.1:12060`.

While the settings section is open, the radios heard on the address are
listed under the source.  Click one to follow it.  **N1MM radio** is N1MM's
number for the radio, which need not be the same as its MOAS number.  If
several computers send to one address, set the advanced **station name** to
tell them apart.

The address is where moas-web-client listens.  To hear a logger on another
computer, use `0.0.0.0:12060` and send to this computer's address.  Anyone
who can reach that port can then send it a frequency, so keep it to a network
you trust.

Two settings are off unless you turn them on:

* **Follow N1MM's antenna changes** steps to the next antenna in the band's
  relative list when N1MM moves to its next antenna (Alt-F9).  A change that
  comes with a band change is left to the server, which picks an antenna for
  the new band itself.
* **Send F-key text** passes the caption of the F-key message being sent to
  the server, which shows it to the other clients serving that radio.

### DXLog.net

In DXLog, turn on Options → Broadcast → Radio information.  It sends to
`127.0.0.1:12060` by default.  Setting up a source works as it does for N1MM,
and **Follow DXLog's antenna changes** is available too.

N1MM and DXLog can send to the same address, since each source follows only
its own logger.

DXLog does not say when a radio is not connected, so a radio set up in DXLog
but switched off keeps the frequency DXLog last showed for it.

### rigctld

Start hamlib's rigctld for each radio, with its own port:

```
rigctld -m MODEL -r PORT -s BAUD -t 4532
```

`rigctl -l` lists the model numbers.  Enter the address in the source and
press **Test**, which reads the frequency once.  moas-web-client asks four
times a second by default.

### N3FJP programs

In Amateur Contact Log or another N3FJP program, turn on Settings →
Application Program Interface → TCP API Enabled.  **Test** reads the
frequency once.  Other programs can use N3FJP's API at the same time.

N3FJP reports the frequency of the rig in use, and sends it as soon as it
sees a change.  How quickly that is depends on N3FJP's own rig interface
(every 2 to 4 seconds with Amateur Contact Log), so the frequency follows the
radio less closely than with rigctld.

### Falling back to another source

N1MM and DXLog readings never expire by default, so a logger that stops
sending keeps its place as the followed source.  To fall back to a lower
source when that happens, set the advanced **Timeout** for the logger's
source, longer than the longest time it stays silent.  rigctld and N3FJP
sources have timeouts by default.

## Settings

Press **⚙ Settings** to change settings on the page.  Nothing changes until
you press **Save**, and then everything takes effect at once except the page
address, which needs a restart.  **Show advanced options** shows the settings
marked below.

### The settings file

The settings are kept in `moas-web-client.json`.  The program uses the first
of these and prints which:

1. the file given with `-config`;
2. `moas-web-client.json` beside the program, if there is one (on FreeBSD and
   Linux, only when the program was started by a path, not found on the
   `PATH`);
3. `moas-web-client.json` in your settings folder: `%APPDATA%\moas-web-client`
   on Windows, `~/.config/moas-web-client` on FreeBSD and Linux.

To keep the program and its settings together, for example on a USB stick,
create a file beside the program.

You can edit the file by hand; changes take effect the next time the program
starts.  Every entry is optional, and
[moas-web-client.example.json](../moas-web-client.example.json) shows them
all.  `_comment` entries are ignored, but saving from the page does not keep
them.

* If the file has a mistake, the program says so, runs on the defaults and
  leaves the file alone.  The settings section marks the mistake.
* If the file changed after you opened the settings section, saving is
  refused rather than overwriting that change.
* A read-only file is never replaced.

### General settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `Server` | `127.0.0.1:12059` | The MOAS server |
| `Station` | `1` | The station number, 1 to 255 |
| `Radios` | radios 1 and 2 | The radios to show |
| `Listen` (advanced) | `127.0.0.1:8080` | Where to serve the page |
| `Rows` (advanced) | `6` | Grid rows to ask the server for, 1 to 64 |
| `Columns` (advanced) | `5` | Grid columns to ask the server for, 1 to 64 |
| `ShowTXRX` (advanced) | `true` | Ask for and show transmit/receive state |

`Rows` and `Columns` give the server the grid to lay its buttons out in;
change them if the layout does not suit your configuration.

Each entry in `Radios` has `Radio`, from 1 to 255, and a list of `Sources`.

### Source settings

Every source has `Type` (`n1mm`, `dxlog`, `rigctld` or `n3fjp`), `Address`
and `TimeoutSeconds`, which is the age at which a reading stops counting.

| Setting | Applies to | Default | Meaning |
|---------|------------|---------|---------|
| `TimeoutSeconds` | `n1mm`, `dxlog` | never | At most 3600 |
| `TimeoutSeconds` | `rigctld` | `2` | At most 3600 |
| `TimeoutSeconds` | `n3fjp` | poll + 3 | At least the poll plus 3 seconds, at most 3600 |
| `RadioNr` | `n1mm`, `dxlog` | the MOAS radio number | The logger's number for the radio |
| `StationName` | `n1mm`, `dxlog` | any | Follow only this computer's logger |
| `FollowAntenna` | `n1mm`, `dxlog` | `false` | Follow the logger's antenna changes |
| `SendFKeyText` | `n1mm` | `false` | Pass F-key captions to the server |
| `PollMilliseconds` | `rigctld` | `250` | How often to ask, 50 to 60000 |
| `PollMilliseconds` | `n3fjp` | `2000` | How often to ask, 1000 to 60000 |

### Command-line options

Options are optional and win over the file until settings are saved from the
page.  They may also be written `--name` or `-name=value`.

| Option | Meaning |
|--------|---------|
| `-config FILE` | The settings file |
| `-server ADDRESS` | The MOAS server |
| `-listen ADDRESS` | Where to serve the page |
| `-station N` | The station number |
| `-radios LIST` | The radios to show, such as `1,2`; not with a file that lists radios |
| `-rows N` | Grid rows to ask the server for |
| `-columns N` | Grid columns to ask the server for |
| `-watch` | Print what the server sends for one radio; see [Troubleshooting](#watch-mode) |
| `-radio N` | The radio to watch |
| `-version` | Print the version |
| `-help` | Print the options |

## Using the page from other computers

By default the page can be opened only on the computer moas-web-client runs
on.  To serve it to your network, set the page address to `0.0.0.0:8080`, or
start with `-listen 0.0.0.0:8080`, then open
`http://ADDRESS-OF-THIS-COMPUTER:8080/` on the other computer.  Your firewall
may need to allow the port.

**The page has no login and no encryption, because the MOAS protocol has no
login.  Anyone who can open the page can switch antennas.**  Serve it only on
a network you trust, and do not forward the port from the internet.

Settings can be changed only on the computer moas-web-client runs on, so the
other computers do not see the settings button.  The program prints a warning
when it starts if the page can be reached from elsewhere.

## Troubleshooting

### Watch mode

Watch mode prints everything the server reports for one radio: the buttons,
what is selected, and transmit/receive changes.

```
moas-web-client -watch -server 192.168.1.10:12059 -station 1 -radio 1
```

It sends nothing but its greeting, so it never changes what the switch does.
It takes the server and station from the settings file if you leave them out.
A radio whose frequency the server does not know yet has no buttons; watch
mode says so.

### Common problems

**"Not connected to the MOAS server."**  Check that the server is running and
that the address and port are right, and that a firewall is not in the way.
moas-web-client keeps trying.

**"The MOAS server refused this client."**  The server allows only one client
and another is connected.  moas-web-client asks again every minute.

**"Lost contact with the client program."**  moas-web-client has stopped, or
the page's computer cannot reach it.  The page reconnects when it can.

**The program says "cannot serve the page".**  Something else is using the
port, often another copy of moas-web-client.  Stop it, or use `-listen` with
another port.

**An address is refused as "not host:port with a numeric host".**  Use a
numeric address such as `192.168.1.10:12059`, or `localhost`.  IPv6 addresses
go in brackets, such as `[::1]:8080`.

**Every radio shows TX/RX NOT REPORTED, or INHIBITED that never changes.**  A
MOAS server starts every radio as inhibited, and only a switch that reports
transmit and receive changes that.  Add `"IndicateTR" : true` to the switch's
entry under `"Switches"` in the server's configuration and restart the server.
To show no transmit/receive state at all, turn off **Show transmit/receive
state** in the advanced settings.

**No buttons, and "no band yet".**  The server does not know the frequency
yet, or it is outside the bands in the server's configuration.  Check the
source's status under the radio's name, or type a frequency.

**"Nothing heard from N1MM" or "from DXLog".**  Check the logger's broadcast
setting and that the logger sends to the source's address.  Only one program
can usually listen on a UDP port, so check that no other program on this
computer has taken it.

**A logger's radio shows "not connected".**  N1MM says the radio is not
connected to it; check N1MM's own connection to the radio.

**A rigctld source shows "connection refused".**  rigctld is not running, or
is on another port than the source's address.

**An N3FJP source shows "connection refused" or "timed out connecting".**
The N3FJP program is not running, its TCP API is not turned on, or it listens
on another port than the source's address.  "No usable frequency" means N3FJP
has no frequency to show: enter one in its frequency field, or set up its rig
interface.

**Saving says the file is read-only.**  Change the file's permissions, or edit
it by hand.

**A setting from the file is not being used.**  An option on the command line
wins over the file.  The settings section lists any options that did.
