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

- A C++20 compiler with module support. This project is developed against
  **GCC with `-fmodules-ts`** (GCC 14+ recommended; tested with GCC 15).
  Module support in GCC is still evolving, so other compilers (Clang, MSVC)
  are not currently supported.
- A POSIX shell (Linux or macOS; on Windows use WSL). The build and bootstrap
  scripts are plain `sh` scripts that shell out to the compiler directly —
  there is no CMake, Make, or other build system underneath them.

Check your compiler before starting:

```sh
c++ --version
c++ -std=c++20 -fmodules-ts -x c++ -c /dev/null -o /dev/null && echo "modules OK"
```

## Quick start

From a clean checkout:

```sh
./bootstrap.sh    # builds out/build0 and out/build1 using direct compiler commands
./build.sh        # uses build1 to compile the full project into out/bin
./test.sh         # runs the smoke and regression tests
./document.sh     # generates HTML docs under out/ from the MDY manifests
```

`bootstrap.sh` exists because the real build tool (`build`) is itself part of
this project and written using modules — bootstrap compiles just enough by
hand to produce a working `build` binary, which then builds everything else,
including itself. This is what "self-hosting" means in this project.

If something fails partway through, `./clean.sh` removes all generated output
(the `out/` and `gcm.cache/` directories) so you can start over.

## Everyday commands

Once bootstrapped, `out/bin/build` and `out/bin/test` are the tools you use
day to day, pointed at a manifest file (`mm.mdy`):

```sh
./out/bin/build mm.mdy               # build the whole project
./out/bin/build -v modules/mm.mdy    # build one subtree, verbose output
./out/bin/test tests/mm/build/mm.mdy # run one test target
./out/bin/test -v tests/mm/mdy       # run a test directory, verbose output
```

`-v` / `--verbose` prints extra diagnostic output and is supported by all of
the project's tools (`build`, `test`, `mdy`).

## Layout

- `apps/` — example and utility applications (`main`, `mdy`).
- `modules/mm/` — the reusable core modules (parsing, build graph, test
  framework, application base class).
- `tools/` — command-line front ends (`build`, `test`) that use those
  modules.
- `tests/mm/` — public integration and regression tests.
- `docs/` — the project's own documentation, written in MDY and rendered by
  the `mdy` app. Start with [docs/modules.mdy](docs/modules.mdy) for the full
  developer guide, and [docs/mdy.mdy](docs/mdy.mdy) for the MDY format itself.
- `*.sh` — the bootstrap, build, test, clean, and document scripts described
  above.

## Learn more

[docs/modules.mdy](docs/modules.mdy) covers the architecture, the MDY
manifest format, every core module and tool in detail, and the TDD workflow
for making changes. This README only covers getting the project running for
the first time.
