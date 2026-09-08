# modules.cpp

modules.cpp is a small, self-contained C++20 project that builds, tests, and
documents itself from source — no third-party build system, package manager,
or test framework. It uses **C++20 modules** instead of `#include` headers,
and drives everything through its own manifest format (MDY) and its own
build/test/doc tools.

If you're new to C++20 modules: instead of

```cpp
#include "foo.h"
```

a module is declared and imported like this:

```cpp
// foo.cppm — a module interface file
export module foo;
export int add(int a, int b) { return a + b; }
```

```cpp
// main.cpp
import foo;
int main() { return add(1, 2); }
```

No header guards, no textual copy-paste of declarations — the compiler reads
the module's interface directly. modules.cpp is organized entirely this way;
see [docs/modules.mdy](docs/modules.mdy) for the full architecture.

## Prerequisites

- A C++20 compiler with module support. Self-hosted builds support GCC and
  Clang; bootstrap remains the fixed GCC-oriented recovery path through `c++`.
  GCC 14+ or a recent Clang release is recommended.
- A POSIX shell (Linux or macOS; on Windows use WSL). The build and bootstrap
  scripts are plain `sh` scripts that shell out to the compiler directly —
  there is no CMake, Make, or other build system underneath them.

Check a GCC compiler before bootstrapping:

```sh
c++ --version
c++ -std=c++20 -fmodules-ts -x c++ -c /dev/null -o /dev/null && echo "modules OK"
```

## Quick start

From a clean checkout:

```sh
./bootstrap.sh    # builds out/build0 and out/build1, then runs build1 to
                   # compile and install the full project, out/bin/build included
./build.sh        # rebuilds via out/bin/build, e.g. after a later source change
./test.sh         # runs the smoke and regression tests
./document.sh     # generates HTML docs under out/ from the MDY manifests
```

`bootstrap.sh` exists because the real build tool (`build`) is itself part of
this project and written using modules — bootstrap compiles just enough by
hand to produce a working `build1` binary (trying `build0 build1` first,
falling back to hardcoded compiler commands if that fails or produces
nothing), then runs it, which builds everything else, `out/bin/build`
included. This is what "self-hosting" means in this project. `build.sh` is
not needed on a fresh checkout — bootstrap.sh already leaves a fully built
project — but is what you run afterward, once `out/bin/build` exists, on any
later change.

Bootstrap always uses the compiler named `c++`. After bootstrapping, select
one project compiler and build with `configure`; both build and test read the
resulting `out/config.mdy` and cannot select different values:

```sh
./configure --compiler gcc-15 --build debug
./build
./test

./configure --compiler clang++-20 --build release
./build
./test

./configure --target m68k-linux-gnu --target-host \
    --compiler m68k-linux-gnu-g++-16 --runner qemu-user --debugger gdb --build release
./build --target
./run --target apps/main -- -h
./debug --target apps/main
```

Accepted compiler selectors are `gcc`, `g++`, `clang`, and `clang++`, with an
optional numeric major-version suffix. C-driver spellings such as `gcc-15`
are normalized to their C++ driver (`g++-15`). `--build` accepts `debug` or
`release` and defaults to `debug`. Bootstrap output stays under `out`;
configured native output goes to `out-host`, regardless of compiler family
or build. `./clean.sh` removes both.

If something fails partway through, `./clean.sh` removes all generated output
(the `out/` and `gcm.cache/` directories) so you can start over.

## Everyday commands

Once bootstrapped, `out/bin/build`, `out/bin/test`, and `out/bin/run` are the tools you use
day to day, pointed at a manifest file (`mm.mdy`):

```sh
./out/bin/build mm.mdy               # build the whole project
./out/bin/build -v modules/mm.mdy    # build one subtree, verbose output
./out/bin/test tests/mm/build/mm.mdy # run one test target
./out/bin/test -v tests/mm/mdy       # run a test directory, verbose output
./test -v                            # run every target with verbose toolchain output
./run --host apps/main -- -v         # run an already-built application
```

`-v` prints extra diagnostic output and is supported by all of the project's
tools (`build`, `configure`, `test`, `run`, `check`, `model`, `shell`, `mdy`); `--verbose` is
also accepted by `build`, `configure`, `test`, `run`, `check`, `model`, and `shell`, but not by
`mdy`, which only recognizes `-v`.

## Layout

- `apps/` — example and utility applications (`main`, `mdy`).
- `modules/mm/` — the reusable core modules (parsing, build graph, test
  framework, application base class).
- `tools/` — command-line front ends (`build`, `configure`, `test`, `run`) that use those
  modules.
- `tests/mm/` — public integration and regression tests.
- `docs/` — the project's own documentation, written in MDY and rendered by
  the `mdy` app. Start with [docs/modules.mdy](docs/modules.mdy) for the full
  developer guide, and [docs/mdy.mdy](docs/mdy.mdy) for the MDY format itself.
- `*.sh` — the bootstrap, build, test, clean, and document scripts described
  above. Each also has a no-extension counterpart at the project root
  (`build`, `test`, ...) that runs the same command through the project's
  own shell tool instead of the system shell directly; see
  docs/modules.mdy's "Shell tool wrapper scripts".

## Learn more

[docs/modules.mdy](docs/modules.mdy) covers the architecture, the MDY
manifest format, every core module and tool in detail, and the TDD workflow
for making changes. This README only covers getting the project running for
the first time.

[Configure specification for release v1.1.0](docs/modules-configure.mdy) defines
the official manifest-option, reset, and read-only requirements. Its Current
boundaries section describes the implemented capability-only scope; build,
test, run, and debug warn about tuning declarations but do not apply their
values yet.
