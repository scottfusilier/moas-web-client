# Contributing

Thank you for helping.  This file covers building, testing, the code style
and the rules the code follows.  [docs/ROADMAP.md](docs/ROADMAP.md) has the
plan and the reasons behind the main decisions.

## Building

You need git, CMake 3.21 or later, Ninja, and a C11 compiler.  For now the
project is built and tested on Windows, FreeBSD and Linux.  It is written to
run on macOS too, and the build files have MSVC settings, but those are not
tested yet.  Ninja is the build tool everywhere.

Each system below goes from nothing to a built and tested program.  Use the
`release` build to run moas-web-client; the others are for working on the code.

### FreeBSD

```sh
pkg install git cmake ninja        # as root; clang is in the base system
git clone https://github.com/scottfusilier/moas-web-client.git
cd moas-web-client
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The program is `build/release/src/app/moas-web-client`.

### Linux

Debian and Ubuntu:

```sh
sudo apt install git build-essential cmake ninja-build
```

Fedora: `sudo dnf install git gcc cmake ninja-build libasan libubsan`.

Then, as on FreeBSD:

```sh
git clone https://github.com/scottfusilier/moas-web-client.git
cd moas-web-client
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The program is `build/release/src/app/moas-web-client`.  CMake uses `cc`,
which is gcc on most distributions; that also gives the project a second
compiler family.  For fuzzing, also install clang (`apt install clang`, and
`libclang-rt-dev` if linking fails).

### Windows

1. Install [MSYS2](https://www.msys2.org) in `C:\msys64`.
2. Open the **MSYS2 CLANG64** shell from the Start menu and install the
   tools:

   ```sh
   pacman -S git mingw-w64-clang-x86_64-toolchain \
       mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja
   ```

3. In the same shell, build and test:

   ```sh
   git clone https://github.com/scottfusilier/moas-web-client.git
   cd moas-web-client
   cmake --preset release
   cmake --build --preset release
   ctest --preset release
   ```

The program is `build\release\src\app\moas-web-client.exe`.  It needs only
what comes with Windows 10 and later, so it can be copied to another
computer and run there without MSYS2.

To build from PowerShell instead, put MSYS2's tools first on `PATH` and name
the compiler, for that window only:

```powershell
$env:PATH = "C:\msys64\clang64\bin;$env:PATH"
$env:CC = "clang"
cmake --preset release
```

Naming the compiler matters if another clang is installed, such as LLVM's
own in `C:\Program Files\LLVM`: CMake could find that one first, and it
cannot build here on its own.  `scripts\check.bat` sets this up itself.

### Optional tools

* **clang-format** (version 23 is the reference) for the formatting check;
  without it, the check scripts skip formatting.  On Windows it is part of
  the MSYS2 toolchain above.  FreeBSD's base system does not have it, so
  formatting is checked on Windows or Linux.
* **MSYS2's gcc** on Windows (`pacman -S mingw-w64-ucrt-x86_64-gcc`) adds a
  gcc pass to `check.bat`; see below.
* **gcc on FreeBSD** (`pkg install gcc14`); see below.

### Presets

| Preset    | What it builds |
|-----------|----------------|
| `debug`   | Debug build: no optimisation, debug information |
| `release` | Optimised build, with `_FORTIFY_SOURCE` where available |
| `asan`    | Debug build with AddressSanitizer and UndefinedBehaviorSanitizer |
| `fuzz`    | The fuzz targets with libFuzzer and the sanitizers (clang, FreeBSD and Linux) |

```
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Each preset builds in `build/<preset>/`.  Presets treat compiler warnings as
errors; a plain `cmake -S . -B build` does not, unless you add
`-DMOAS_WERROR=ON`.

### Checking a change

`scripts/check.sh` (FreeBSD, Linux, or an MSYS2 shell) and
`scripts\check.bat` (Windows) build and test all three presets and check the
formatting.  A change is done when the check passes on Windows, FreeBSD and
Linux.

On Windows, if MSYS2's gcc is also installed (`pacman -S
mingw-w64-ucrt-x86_64-gcc`), `check.bat` also builds an optimised gcc build
with warnings as errors and `_FORTIFY_SOURCE`, and runs the tests on it:
gcc warns about things clang does not, especially in optimised code.

To try gcc on FreeBSD, `pkg install gcc14` and run the check with
`CC=gcc14` (after `rm -rf build`).  gcc's sanitizers will not start on
FreeBSD with address randomisation on; run the `asan` tests with
`proccontrol -m aslr -s disable ctest --preset asan`.

### Fuzzing

Every parser of outside input has a fuzz target in `fuzz/`.  On FreeBSD and
Linux, `scripts/fuzz.sh` builds them with clang and libFuzzer and runs each
for 60 seconds (`scripts/fuzz.sh 600` for ten minutes).  Run it after
changing a parser.  Interesting inputs it finds are kept in
`build/fuzz/corpus/`, so later runs carry on; a crashing input is saved in
`build/fuzz/crashes/`.  Once the bug is fixed, copy that input into
`fuzz/corpus/<target>/`.

libFuzzer is not available with MinGW on Windows.  There, and in every
normal build, each target instead replays the inputs in
`fuzz/corpus/<target>/` as an ordinary test (`fuzz_<target>_replay`).

## Code style

The layout follows [PEP 7](https://peps.python.org/pep-0007/), the style
guide for the C code of Python:

* 4-space indents, never tabs.
* Lines of at most 79 characters.
* Braces around every `if`, `else`, `for`, `while` and `do` body, even a
  single statement.
* K&R braces, except that a function's opening brace goes on a line of its
  own, and so does the brace of a control statement whose condition runs
  over several lines.
* In a function definition, the return type goes on a line of its own:

  ```c
  static int
  parse_khz(const char *s, size_t len, long *khz)
  {
  ```

* Declare variables where they are first used.
* `/* */` comments.
* In printf-style formats, no `%zu`, `%jd` or `%td`: gcc on Windows checks
  formats against Microsoft's rules, which lack them.  Cast to `unsigned
  long long` or `long long` and use `%llu` or `%lld`.

Naming is not PEP 7's (that part is specific to Python):

* `lower_snake_case` for functions, variables and types.
* A prefix naming the module: `plat_`, `loop_`, `buf_`, `oasp_`, `freq_`,
  `http_`, `app_`.
* `UPPER_CASE` for macros and constants.

`.clang-format` applies the layout; the reference is clang-format 23.  Run `cmake --build --preset debug --target format` to format your
changes, or `--target format-check` to see what would change.
`.editorconfig` sets up most editors to match.

Every source file starts with:

```c
/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Scott Fusilier
 */
```

## Rules the code follows

* **Layers.** Each part of `src/` is a library that may use only the layers
  below it: `platform/`, then `core/`, then `oasp/`, `freq/` and `http/`
  (which do not use each other), then `app/`.  CMake enforces this through
  what each library links against.
* **System-specific code** goes only in `src/platform/`, which is the only
  place `#ifdef _WIN32` may appear.
* **Ownership.** Every type has `x_create()` and `x_destroy()`; whoever
  creates something destroys it.  Small types that live inside other structs
  (`struct buf`, `struct loop_timer`) have `x_init()` and `x_fini()`
  instead.  A callback may use the data it is given during the call but must
  not keep it.
* **Errors.** Functions return 0 or an error code from `src/platform/err.h`,
  and results come back through output parameters.
* **Untrusted input** (from the network, the browser or the settings file)
  goes through a parser with hard limits.  It stays in (pointer, length)
  slices until checked, and is only then copied into owned memory.  Every
  parser has a fuzz target.
* **Limits** live in one header, so all of them can be reviewed in one
  place.
* **Banned functions.** These C library functions must not be used, and the
  `banned_functions` test fails if they are:

  | Banned | Use instead |
  |--------|-------------|
  | `strcpy`, `strncpy`, `strcat`, `strncat` | the project's buffer type, or `memcpy` with checked lengths |
  | `sprintf`, `vsprintf` | `snprintf`, checking the result, or the buffer type's formatted append |
  | `gets` | never needed |
  | `strtok` | slices, or `strcspn`/`strspn` |
  | `atoi`, `atol`, `atoll`, `atof` | the project's range-checked number parsing |
  | `tmpnam`, `mktemp` | the platform layer's file functions |

  These are allowed only in `src/platform/`, because on Windows they take
  paths and names in the local code page rather than UTF-8; elsewhere, use
  the platform layer (`src/platform/fs.h`):

  | Platform only | Use instead |
  |---------------|-------------|
  | `fopen`, `freopen` | `plat_read_file()`, `plat_replace_file()` |
  | `remove`, `unlink` | `plat_remove_file()` |
  | `rename` | `plat_replace_file()` |
  | `mkdir`, `rmdir` | `plat_make_dir()` |
  | `getenv` | a platform function for what is wanted, such as `plat_settings_dir()` |

## Tests

Tests are in `tests/`, one program per module, using the small harness in
`tests/test.h`.  `ctest` runs them all.  Add tests with every change, and a
test that reproduces a bug before fixing it.

## Dependencies

The only third-party code is in `third_party/`, at pinned versions;
[third_party/README.md](third_party/README.md) lists each library, its
license and why it is there, and the rules a new one must meet.
