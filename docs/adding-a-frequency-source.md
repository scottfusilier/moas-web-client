# Adding a frequency source

A frequency source is a program that can tell moas-web-client a radio's
frequency: N1MM Logger+ and hamlib's rigctld so far.  Each kind of source
is a *type*.  Adding one means writing one file and adding one line to
`src/freq/registry.c`; the tracker, the settings code and the rest of
moas-web-client do not change.  This guide uses the rigctld type
(`src/freq/rigctld.c`) as the example.

## How sources fit in

Each radio has a *tracker* (`src/freq/tracker.h`) holding its sources in
priority order.  A source reports through the *feed* the tracker gives it:

```c
freq_feed_report(feed, 14025);                  /* a reading in kHz */
freq_feed_fail(feed, "rigctld at ...: no reply");  /* cannot read the radio */
```

The tracker decides which source to believe: the first one that is up.  A
source never needs to know about the other sources, the radio's priorities,
or the MOAS server.

Sources run on the event loop (`src/core/loop.h`), like everything else.  A
source must never block: it uses non-blocking sockets from
`src/platform/net.h`, waits for them with `loop_watch()`, and keeps time
with `loop_timer_start()`.

## 1. The settings

A source's settings are one object in a radio's `"Sources"` list in the
settings file, for example:

```json
{ "Type": "rigctld", "Address": "127.0.0.1:4533", "PollMilliseconds": 250 }
```

Your type reads its own settings, so it can have any it needs.  Write a
`parse` function:

```c
static int
rigctld_parse(const cJSON *json, int moas_radio,
              struct freq_source_config *config,
              struct freq_field_error *err)
```

It must:

* Refuse settings it does not know.  `freq_check_keys(json, allowed, err)`
  does that, given a NULL-terminated list of the names you accept ("Type"
  and "_comment..." are always allowed).  If a setting belongs to another
  type, say so first, with a clearer message; rigctld does this for
  `RadioNr`.
* Read each setting with the getters in `src/core/json.h`
  (`json_get_int()`, `json_get_text()` and so on), which check types and
  ranges.  Use `freq_parse_address()` for an address and
  `freq_parse_timeout()` for `TimeoutSeconds`.
* Fill in defaults for anything missing.
* Report a problem with `freq_field_error(err, "Field", "message", ...)`,
  which returns `ERR_INVALID`.  The settings page shows the message next to
  the field, so write it for the user: "PollMilliseconds must be at least
  50".
* Set `config->name` (how the source is shown on the page, such as
  "rigctld 127.0.0.1:4533"), `config->timeout_ms` (how old a reading may be
  before it stops counting; 0 for never), and `config->data` (your own
  settings struct, allocated with `calloc()`).

If `parse` fails after allocating `config->data`, leave it there:
`free_data` is called for you.

Then write `write`, the other way round: it puts your settings back into a
JSON object, which already holds `"Type"`, with the defaults filled in.
moas-web-client uses it to save the settings file and to show the settings
on the page, so whatever `write` produces must read back through `parse` as the
same settings (the `fuzz_settings` target checks this):

```c
static int
rigctld_write(const struct freq_source_config *config, cJSON *obj)
{
    const struct rigctld_config *cfg = config->data;
    bool ok = cJSON_AddStringToObject(obj, "Address", cfg->addr_text) !=
                  NULL &&
              cJSON_AddNumberToObject(obj, "TimeoutSeconds",
                                      config->timeout_ms / 1000.0) != NULL &&
              cJSON_AddNumberToObject(obj, "PollMilliseconds", cfg->poll_ms) !=
                  NULL;
    return ok ? 0 : ERR_NOMEM;
}
```

A setting that is optional and was not given (N1MM's `StationName`, for
example) may be left out rather than written empty.

## 2. Running

Write `start` and `stop`:

```c
static int rigctld_start(struct freq_env *env,
                         const struct freq_source_config *config,
                         struct freq_feed feed, struct freq_source **out);
static void rigctld_stop(struct freq_source *source);
```

Your running source is a struct that begins with `struct freq_source`:

```c
struct rigctld_source {
    struct freq_source base;   /* first, so the two pointers convert */
    struct rigctld_conn conn;
    struct freq_feed feed;
};
```

`start` allocates it, sets `base.ops` to your table, keeps the feed, and
gets going.  `freq_env_loop(env)` gives the event loop.  Do not call the
feed from `start` itself, except to report a failure that is already known;
begin the real work on the loop's next pass by starting a timer with a
delay of 0, as rigctld does.

`stop` closes sockets, stops timers (`loop_unwatch()`,
`loop_timer_stop()`), and frees the struct.  After `stop` returns, the feed
must never be called again.

When something goes wrong, call `freq_feed_fail()` with a description that
says where ("rigctld at 127.0.0.1:4533: connection refused"), then try again
after a pause.  rigctld closes its connection and reconnects after 2
seconds.

## 3. Reading what comes in

Anything a source reads from the network, a serial port or another program
is untrusted.  Put the code that interprets it in a file of its own with no
I/O, as rigctld does with `rigctld_reply.c`:

```c
int rigctld_parse_reply(struct str line, int *khz, int *report);
```

That keeps the risky code small and lets it be fuzzed.  Then:

* Add a fuzz target, `fuzz/fuzz_<type>_<what>.c`, which calls the parser
  with whatever libFuzzer generates, and list it in `fuzz/CMakeLists.txt`.
* Add a few real examples to `fuzz/corpus/<target>/`.
* Run `scripts/fuzz.sh` on FreeBSD or Linux.

Follow the rules in CONTRIBUTING.md: slices (`struct str`) until the input
is checked, limits from `src/core/caps.h`, text cleaned with
`str_copy_clean()` before it is kept or shown.

## 4. Sharing between sources (if needed)

Some types need one thing shared by several sources: all the radios on one
N1MM address share a single UDP listener.  `freq_env_shared(env, &ops)`
gives your type a pointer to keep that in; set `env_fini` to free it.  See
`src/freq/n1mm.c`.  Most types, like rigctld, do not need this and leave
`env_fini` NULL.

## 5. Registering

Put the table at the end of your file:

```c
const struct freq_source_ops freq_rigctld_ops = {
    .type = "rigctld",
    .parse = rigctld_parse,
    .free_data = rigctld_free_data,
    .write = rigctld_write,
    .start = rigctld_start,
    .stop = rigctld_stop,
    .env_fini = NULL,
};
```

declare it in your header (`extern const struct freq_source_ops ...`), add
it to `types[]` in `src/freq/registry.c`, and add your type's name to
`freq_type_list()` there.  Add your `.c` files to `src/freq/CMakeLists.txt`.

## 6. Tests

Add `tests/test_<type>.c` (and a line in `tests/CMakeLists.txt`).  Test the
parser with good and bad input, the settings (defaults, each error
message, and that `write` reads back the same), and the running source
against a stand-in for the real program on the loopback interface, on the
same event loop.  `tests/test_rigctld.c` does all of this, with the
stand-in in `tests/fake_rig.c`.  Add a sample settings file using your type
to `fuzz/corpus/settings/`.  Then run `scripts/check.sh` (or
`scripts\check.bat`).
