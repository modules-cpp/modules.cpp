# modules.cpp v1.3.0

Something to run sketches with. v1.2.4 gave a program sound; v1.3.0 gives the
project a language of its own to be driven in and a door for code written for
another platform: `.ino` sketches build as ordinary modules.cpp applications,
inside the tree or outside it, over `mm.sketch`'s Arduino-compatible
vocabulary; a shell of the project's own runs the tracked scripts and the root
launchers without `/bin/sh`, and scales down to an allocation-free embedded
shell on a Pico; and `mm.parse` gives both shells and the JSON and configure
readers one scanner and one number reader. Manifests gain `mm: 1.3`.

```sh
out/bin/sketch apps/ino/blink && ./build apps/ino/blink/
out/bin/sketch --project . ~/Arduino/MySketch && out/bin/build ~/Arduino/MySketch
out/bin/shell --run scripts/build-gfx.sh -- --dry-run
./check.sh --strict
```

A sketch is a directory of `.ino` files. `tools/sketch` writes its
`mm.mdy` and a `main.cpp` that `mm.ino` generates from the sketches -- the
prelude, the includes a sketch leaves to its toolchain, prototypes for every
function, the bodies with line markers so diagnostics point at the `.ino`, and
a `main` that runs `setup` once and `loop` until `requestExit`. `mm.sketch` is
the vocabulary it runs against, over `mm.mcu` and `mm.stdio`: pins, analog
reads and writes, tones, pulses, time, the math and character functions,
random numbers, bits and bytes, external interrupts, `Serial`, `SPI`, `Wire`,
`Stream`, `String`, and `Print`. A vendored library that includes `Arduino.h`
gets a generated compatibility header and compiles unchanged. With `mm: 1.3`'s
`project:` key a sketch outside the checkout builds, runs, flashes, and is
checked with this project's toolchain, writing only beneath its own tree;
`sketch-library:` lets a library's example sketches find the library they
exercise. `apps/ino` has blink, button, and echo.

The project shell comes in four levels. `mm.shell` is the level-1 foundation
that builds for a target: no allocation, no file system, every buffer the
caller's, and a fixed boundary script in `apps/shell-smoke` that proves the
capacities it declares. `mm.shell.mcu` adds `mm.mcu` and `mm.stdio` for an
interactive shell on a board; on an RP2040 it adds 110 KiB of flash over a
shell-free control and uses 57 KiB of static RAM, inside its stated ceilings. `mm.shell.full`
is the host profile -- pipelines, redirections, here-documents, subshells,
traps, functions, command substitution, and now `.` -- written against
abstract services, and `tools/shell` runs it natively. The thirteen root
launchers run their scripts through it, and every tracked script is compared
with dash for status, output, files written, and the environment a child tool
sees. `--legacy-sh`, or `MM_SHELL_LEGACY=1`, restores `/bin/sh` for this
release only.

`mm.parse` is the scanner both shells now share, a number reader, and a time
reader. The move to it had left `main` unbuildable and, once built, wrong in
ways its tests had not been run to catch; this release fixes every one and
runs every suite in the tests manifest, including the four `test.sh` had been
missing. `mm.build` is split into partitions with tests to match, `./check.sh`
lists each documented exception to the language rules on every run and fails
on them under `--strict`, and v1.2.4's build scripts and audio work arrive
here with it.

**Sketches and the MCU shell have not been run on hardware in this release.**
The sketches, the size measurements, and the Pico lanes are compile and link
qualifications; the M68k and MPS2 QEMU shell smokes pass. **mm.sketch keeps
seven documented departures from the project's language rules** -- three
unscoped enumerations and four templates that Arduino code depends on --
listed by `./check.sh` and recorded in docs/modules-c++20.mdy.

`mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid;
`mm: 1.3` adds `project:`, `sketch:`, and `sketch-library:`.

Requires a C++20 compiler with module support, GCC 14 or newer or a recent
Clang, with GCC 15 or newer recommended, and a POSIX shell. The Pico platforms
require the prebuilt tools that `platforms/pico/install-sdk-tools.sh`
provisions. See [CHANGELOG.md](CHANGELOG.md) for the full list,
`docs/modules-ino.mdy` and `docs/modules-sketch.mdy` for sketches,
`docs/modules-shell.mdy` for the shell, and `docs/modules-parse.mdy` for
`mm.parse`.
