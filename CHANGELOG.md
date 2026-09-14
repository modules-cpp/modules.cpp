# Changelog

All notable changes to modules.cpp. Versions follow [semantic versioning](https://semver.org/).

## [v1.2.0] — 2026-09-14

Targets that are real hardware. v1.1 could cross compile and run a hosted
target under an emulator; a bare-metal target compiled but could not link.
v1.2 adds the three things that were missing — a description of the platform,
a way to consume code the project does not own, and a portable interface to
the machine — so one manifest tree now produces host tools, emulator targets,
and firmware that runs on a Raspberry Pi Pico.

51 commits since v1.1.0; 232 files changed, 19373 insertions, 899 deletions.

### Added

- **Platforms.** `kind: sdk` and `kind: board` manifests describe a target:
  triple, compiler family, runtime, processor properties, linker script, board
  sources, and machine. `configure --sdk` and `--board` select one. A board
  implies its SDK, and `target` is not valid on a board because a board's
  target is its SDK's.
- **Responsibilities.** Five startup obligations — reset vector, initial stack,
  memory layout, runtime init, syscalls — declared by `provides:` on an SDK or
  board. A bare-metal lane must cover all five exactly once; a gap names the
  missing one and an overlap names both claimants, before the linker runs.
- **Libraries.** `kind: library` describes a vendored tree, its licence, and
  its public include interface. `library:` on a module makes it a wrapper and
  on an SDK names the platform implementation. A checkout may be absent, which
  is a valid unselected state rather than an error.
- **External CMake builder.** `external-build: cmake` delegates the final link
  to a project-owned bridge beside the library manifest. Build generates
  `mm-toolchain.cmake` and `mm-inputs.cmake`, validates the bridge's ABI
  projection against the project's own, fingerprints a cache identity over
  every input, and publishes the artifacts the bridge lists in `mm-result.txt`.
- **The core boundary.** `option: core` with `read-only: core` marks a branch
  that may not reach third-party code, machine-checked across `use:` edges and
  for `library:` declarations.
- **`requires-board`.** Pins an application or test to one board. A root build
  skips a mismatch and counts it; an explicit request exits 77.
- **The Pico platform family.** `pico-arm` and `pico-riscv` SDKs, six boards
  (pico, pico-w, pico2-arm, pico2-riscv, pico2-w-arm, pico2-w-riscv), the
  pinned Pico SDK checkout with its CMake bridge and C adapter, the
  `riscv32-pico-elf` toolchain, UF2 validation through real picotool, and
  OpenOCD support. `platforms/pico/install-sdk-tools.sh` provisions the
  prebuilt tools against pinned SHA-256 digests.
- **`mm.mcu`.** A portable, pure C++ microcontroller interface in partitions by
  facility: GPIO, SPI, UART, timer, board. `Unsupported` is a first-class
  answer, so a platform implements what it has.
- **Platform providers.** `platform-interface:` marks a module as a portable
  interface; `platform-provider:` on an SDK or board binds it to an
  implementation. The build injects that provider only into executables whose
  dependency closure reaches the interface, so an unrelated application
  receives no provider object and no static initialiser.
- **`mm.display` and `mm.epaper.ssd1680`.** A portable display boundary and an
  SSD1680 controller with bounded reset and busy handling, packed writes, full
  and partial refresh, and deep sleep, exercised against a recording platform
  without hardware.
- **`flash` tool.** Installs one built target application on a physical board
  through picotool's UF2 loader.
- **Board derivation.** `derives-from:` builds one board on another, inheriting
  its platform identity and specialising the memory map, sources, machine, and
  provider bindings. `boards/` holds a project's own hardware, separate from
  the reference support in `platforms/`.
- **Specifications.** `docs/modules-mm.mdy`, `modules-platforms.mdy`,
  `modules-libraries.mdy`, `modules-cmake.mdy`, `modules-platform-pico.mdy`,
  `modules-platform-mcu.mdy`, `modules-display.mdy`, `modules-epaper.mdy`, and
  `modules-flash.mdy`, each marked with the release it governs.

### Changed

- **Manifest format version 1.2.** Every platform, library, provider, and
  derivation key requires `mm: 1.2`. An older binary rejects such a manifest by
  version rather than assigning a key an older meaning. `mm: 1.3` and `mm: 1.4`
  are rejected as unsupported.
- Provider resolution runs once, after the manifest tree is loaded and before
  either lane tool builds its filtered target tree, because an interface
  requirement decides whether a node survives filtering. `build` and `test`
  consume one analysis rather than approximating it separately.
- Board sources are carried into externally linked lanes, so a board may
  contribute objects to a bridge-owned link.
- The Pico adapter reports the selected board rather than `PICO_BOARD`, so
  `pico2-arm` and `pico2-riscv` are distinguishable at runtime.
- `models::BoardNode` exposes resolved values, the derivation chain, and the
  origin of each value; `model --boards` prints them.
- Generic external-builder code names no library, board, or controller.

### Fixed

- Bare-metal configuration and the Cortex-M33 security domain.
- Pico coupling removed from the generic external build path.
- A UF2 is accepted only when every block carries its magic words, so a bridge
  that copies an ELF under a `.uf2` name fails before publication.

### Known limitations

- **No tool converts a tuning option into a compiler or linker argument.** Only
  `buildable-host`, `buildable-target` and `core` are consumed; the others are
  validated, recorded and reported as ignored.
- Nothing automated asserts that a Pico image ran. Hardware execution is an
  opt-in script whose result a person reads.
- RP2350 RISC-V hardware has not been exercised, and no OpenOCD machine value
  is claimed for it.
- `requires-board` matches exactly; a derived board does not satisfy a
  requirement naming its base.
- A derived board cannot remove one of its base's sources, and multiple bases
  are not supported.
- Arbitrary tri-color image composition is not implemented; the B panel
  initializes and clears its red plane.
- picotool is required, not vendored.
- Builds are full rather than incremental.

### Compatibility

- `mm: 1.0` and `mm: 1.1` manifests are unchanged and still valid.
- The configuration record stays `configuration-2`; its key set is extended,
  and a reader predating a key rejects it by name rather than ignoring it.
- A tree using any 1.2 key requires a tool that supports 1.2.
- **GCC 15 or newer is required.** GCC 14 fails with an internal compiler error
  in `simplify_aggr_init_expr` (`cp/semantics.cc`) compiling `tools/configure`,
  and no source-level workaround avoids it. Continuous integration builds with
  GCC 15 from the Ubuntu toolchain PPA.

## [v1.1.0] — 2026-09-08

Cross compilation. v1.0 built, tested and documented itself with one compiler.
v1.1 adds a configuration step that selects a compiler, a build type and a
target, and the tools that act on it, so one manifest tree can produce host
tools and target artifacts side by side and run and debug the latter under an
emulator.

30 commits since v1.0.1; 86 files changed, 6779 insertions, 406 deletions.

### Added

- **`configure` tool.** Resolves the manifest tree, writes `out/config.mdy`,
  and records resolved settings per node. `build`, `test`, `run` and `debug`
  all read that one file, so they cannot select different compilers.
  `--compiler` accepts `gcc`, `g++`, `clang`, `clang++` with an optional
  version suffix and target-prefixed drivers such as `m68k-linux-gnu-g++-16`;
  `--build` selects `debug` or `release`; `--target`, `--target-host`,
  `--runner` and `--debugger` configure a target lane.
- **Manifest options.** `option:`, `reset:` and `read-only:` keys, inherited
  down the folder tree. Eight registered names: `warnings`, `warnings-error`,
  `optimize`, `debug-info`, `assertions`, `include-dir`, `buildable-host`,
  `buildable-target`. Values are typed rather than raw arguments, so a manifest
  cannot name a compiler, target or linker.
- **`read-only` locks.** Freezes a value for a branch. Redeclaring a locked
  name is an error even when the value would be identical, which keeps
  manifest validity independent of the selected build.
- **Host and target lanes.** `buildable-host` and `buildable-target` say which
  lanes a node can be built for, validated across `use:` edges so a portable
  application cannot depend on a host-only module.
- **`run` tool.** Executes a target binary through the configured runner.
- **`debug` tool.** Connects the configured debugger to a target binary.
  Profiles for `m68k-linux-gnu`: `qemu-user` and `gdb` over a QEMU remote stub.
- **`models.toolchain`.** A toolchain as five roles — compiler, assembler,
  linker, librarian, debugger — bound to programs, where several roles may
  share one program and `invoked()` separates a role a build runs from one that
  is only named.
- Five specifications: `docs/modules-configure.mdy`, `docs/modules-build.mdy`,
  `docs/modules-test.mdy`, `docs/modules-run.mdy`, `docs/modules-debug.mdy`.
- `scripts/release.sh` and GitHub Actions workflows for a clean build on every
  commit and a manually triggered release build.

### Changed

- **Output layout.** `out/` holds bootstrap products, installed commands,
  `out/config.mdy` and generated documentation. A configured native build
  writes to `out-host/`, a target build to `out-target-<triple>/`. A configured
  build never overwrites what bootstrap produced, and a cross-compiled
  executable never replaces a host tool in `out/bin`.
- **Manifest format version 1.1.** A manifest declaring `option:`, `reset:` or
  `read-only:` must declare `mm: 1.1`. Unknown keys are rejected in a 1.1
  manifest and still ignored in a 1.0 one.
- `bootstrap.sh` stages `build0`, `build1` and `configure1`.
- `clean.sh` removes `out-*/` as well as `out/` and `gcm.cache/`.
- Build type defaults and the baseline compile and link flags now come from one
  typed policy rather than separate literals.
- `models.configuration` describes the lane: `host_toolchain()`,
  `target_toolchain()`, `selection()` and the build directories.
- `mm.model` reports the configuration a build would actually follow rather
  than the unconfigured default.
- `--compile-only` on `test` compiles without linking, so it is usable on a
  target that cannot yet link.
- Common CLI flags unified across the tools: `-v`/`--verbose`, `-h`/`--help`.

### Fixed

- `test.sh` smoke tests ran stale binaries from an unconfigured build; they now
  run the installed commands.
- `mm.shell` and `tests/mm/shell` are declared host-only, since `setenv` and
  `unsetenv` have no freestanding implementation.

### Known limitations

- **No tool converts an option into a compiler or linker argument.** Only
  `buildable-host` and `buildable-target` are consumed; the other six are
  validated, recorded and reported as ignored.
- No command-line overrides for compiler, target, build type, flags or output
  directory. `configure` is the only route.
- Runner and debugger support is limited to profiles compiled into `configure`.
- Builds are full rather than incremental.
- **Hosted cross targets work end to end; freestanding targets compile but
  cannot link.** A bare-metal target such as `arm-none-eabi` needs `--specs=`
  and syscall stubs, and there is no route for either yet.

### Compatibility

- `mm: 1.0` manifests are unchanged and still valid.
- `bootstrap.sh` still uses the literal `c++` and needs no configuration.
- An existing `out/config.mdy` keeps the directories it was written with until
  `configure` runs again.

## [v1.0.1] — 2026-09-05

### Changed

- Renamed the core build types for clarity: `Unit` to `TranslationUnit`,
  `Target` to `BuildableNode`, and `Node` to `ManifestNode`, so one concept
  reads under one name in both the concrete and abstract layers.
- Reconciled the documentation with the implementation.

## [v1.0.0] — 2026-09-02

First release. A self-contained C++20 project that builds, tests and documents
itself from source, with no third-party build system, package manager, test
framework or documentation generator. 77 commits from the initial commit on
2026-08-23.

### Added

- **MDY**, a line-oriented manifest and document format, with the `mm.mdy`
  manifest tree that every tool walks: `kind: project`, `dir`, `module`, `app`,
  `test` and `doc`.
- **Bootstrap.** `bootstrap.sh` compiles enough by hand through the literal
  `c++` to produce `build1`, which then builds the rest, including the real
  build tool. A raw-command fallback runs if that fails.
- **Core modules** under `modules/mm`: `mm.mdy` (parser), `mm.build` (manifest
  walk, dependency graph, compiler and linker driver), `mm.app` (application
  boundary and command line), `mm.test` (test framework), `mm.shell` (shell and
  environment), `mm.model` (adapter onto `models/`).
- **Tools**: `build`, `test`, `check`, `model`, `shell`.
- **Applications**: `main` and `mdy`, the documentation renderer.
- **`models/`**, a second independent description of the project's own
  structure as abstract C++20 interfaces: `models.document`,
  `models.configuration`, `models.manifest`, `models.repository`,
  `models.tool`, `models.modules`, `models.workflow`, `models.artifacts`.
- **Documentation** written in MDY and rendered by the project's own tool:
  `docs/modules.mdy`, `docs/mdy.mdy`, `docs/modules-c++20.mdy`,
  `docs/modules-model.mdy`.
- **Workflow scripts**: `bootstrap.sh`, `build.sh`, `test.sh`, `document.sh`,
  `check.sh`, `model.sh`, `clean.sh`, each with a no-extension counterpart that
  runs through the project's own shell tool.
- Test framework with self-registering suites, expected failures reported as
  `xfail`, and `xpass` failing the run when a known defect starts passing.
- GCC and Clang backends, selected per build.

[v1.1.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.1.0
[v1.0.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.1
[v1.0.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.0
