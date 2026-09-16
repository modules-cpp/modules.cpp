# Changelog

All notable changes to modules.cpp. Versions follow [semantic versioning](https://semver.org/).

## [v1.2.1] — 2026-09-15

Hardware device interfaces and a native Linux target. v1.2.0 proved that one
manifest tree produces host tools, emulator targets, and firmware for the
Raspberry Pi Pico. v1.2.1 adds the portable device interfaces that a
composite board binds — touch, inertial sensing, real-time clock, and colour
display — with project-owned controller drivers, a new custom board that
exercises all four at once, a portable console, a native Linux platform, and
the CI fix that keeps the gate honest.

19 commits since v1.2.0; 171 files changed, 12954 insertions, 66 deletions.

### Added

- **`mm.mcu:i2c`.** A portable I2C bus facility in `mm.mcu`: `I2cConfiguration`
  (instance, SDA, SCL, baud), `i2c_configure`, `i2c_write`, `i2c_read`, and
  `i2c_write_read`. The write-read pair is one transaction, not two, so a
  device's address pointer is not exposed to another master between the
  command and the reply. Seven-bit addressing only; ten-bit is a future
  extension when a device that uses it appears.
- **`mm.touch` and `mm.touch.cst328`.** A portable touch input interface and
  the Hynitron CST328 capacitive controller over I2C and GPIO. `read` fills
  as many points as the panel reports into the caller's span; no contact is
  `Ok` with count zero. Coordinates are in the panel's own space; rotation and
  calibration belong to the caller. The driver owns the sixteen-bit
  big-endian register protocol, a bounded reset, and a state machine.
- **`mm.imu` and `mm.imu.qmi8658`.** A portable inertial measurement interface
  and the QST QMI8658 six-axis sensor over I2C. Readings are signed counts in
  the sensor's own axes with a `Scale` that says what a count is worth, so a
  caller converts to whatever units it wants. `read` returns acceleration and
  rotation from one atomic reading. Temperature is hundredths of a degree
  Celsius. The driver owns acceleration and rotation range selection, output
  rate, address auto-detection, and a bounded state machine.
- **`mm.rtc` and `mm.rtc.pcf85063`.** A portable real-time clock interface and
  the NXP PCF85063 I2C clock. `DateTime` is a plain calendar, not an epoch:
  full year, month, day, weekday, hour, minute, second. `read` reports the
  time alongside a `trusted` flag — a clock that lost power still answers
  with well-formed and meaningless fields. The driver owns BCD packing,
  oscillator-stop detection, and a century fix at 2000.
- **`mm.lcd` and `mm.lcd.st7789`.** A colour display controller module for the
  Sitronix ST7789 over SPI and GPIO. Pixels are RGB565 at sixteen bits per
  pixel; `write` places them in the named window and `refresh` answers `Ok`
  because the panel shows what was written when it was written. The board
  supplies the panel's initialisation commands, porch, power, gamma, memory
  access, inversion, and offset. Clear maps the three `mm.display` colours
  onto their RGB565 values.
- **`mm.stdio`.** A portable console: a platform-selected role for moving
  bytes to and from wherever the platform's console goes — USB CDC on Pico,
  descriptors 0 and 1 on Linux. `write` reports how much it accepted; `read`
  with nothing available is `Ok` with count zero; `connected` is a query,
  not a wait. No formatting, no line discipline, no baud. The Pico SDK
  provider uses USB CDC; the Linux provider uses file descriptors with
  zero-timeout input polling, partial output counts, and sticky loss. A
  `stdio-smoke` app exercises the interface.
- **`rp2350_touch_lcd_28` board.** The Waveshare RP2350-Touch-LCD-2.8
  composite board. It binds four platform providers: an ST7789 colour panel
  on `mm.display`, a CST328 touch controller on `mm.touch`, a QMI8658
  inertial sensor on `mm.imu`, and a PCF85063 clock on `mm.rtc`. Each
  provider owns its own wiring; the device drivers are portable modules under
  `modules/mm` and the board is the only place that knows which pin is which.
  A `board-smoke` app names the four interfaces and no board, proving a
  four-provider board links all four in one executable.
- **`display-demo` app.** A portable application that draws three RGB565
  colour bars, holds them, and rotates them twice. It exists so a person can
  tell a working panel from a still one, and so the red bar's encoding
  (0xf800) catches a byte-swap that would draw blue instead.
- **Linux platform.** Native hosted SDKs for `x86_64-linux-gnu` and
  `aarch64-linux-gnu`, implemented directly over libc and the Linux userspace
  kernel ABI. Two boards per architecture: `generic-linux-<arch>` names the
  defaults map provider, and `sdl-linux-<arch>` derives from it and moves the
  screen and pointer into an SDL2 window. Providers cover stdio (file
  descriptors), MCU (GPIO-v2, spidev, I2C_RDWR, termios UART, CLOCK_MONOTONIC),
  RTC (RTC ioctls or CLOCK_REALTIME), display (DRM/KMS dumb buffers, RGB565),
  evdev touch, and IIO. An optional SDL2 backend provides display and touch
  through a window, selected by the board rather than the SDK, so nothing
  pays for it unless a board binds it.
- **Specifications.** `docs/modules-imu.mdy`, `modules-lcd.mdy`,
  `modules-rtc.mdy`, and `modules-touch.mdy`, each marked with the release it
  governs. `docs/modules-platform-linux.mdy` and `modules-stdio.mdy` were
  added in the same series and are included in this entry.

### Fixed

- Continuous integration build: `cppcheck` was not installed, causing the
  `test` step to fail because `tests/mm/configure` exercises the installed
  `check` tool, which wraps `cppcheck` and exits 127 without it.

### Known limitations

- **No tool converts a tuning option into a compiler or linker argument.**
  Carried from v1.2.0; unchanged.
- Nothing automated asserts that a Pico image ran. Carried from v1.2.0;
  unchanged.
- RP2350 RISC-V hardware has not been exercised. Carried from v1.2.0;
  unchanged.
- `requires-board` matches exactly; a derived board does not satisfy a
  requirement naming its base. Carried from v1.2.0; unchanged.
- A derived board cannot remove one of its base's sources, and multiple bases
  are not supported. Carried from v1.2.0; unchanged.
- Arbitrary tri-color image composition is not implemented; the B panel
  initializes and clears its red plane. Carried from v1.2.0; unchanged.
- picotool is required, not vendored. Carried from v1.2.0; unchanged.
- Builds are full rather than incremental. Carried from v1.2.0; unchanged.
- The four device drivers are modelled on Waveshare's reference drivers for
  the RP2350-Touch-LCD-2.8. They drive the protocol but do not include
  vendor headers, board pin numbers, unit conversions, interrupt handlers,
  or rotation transforms. Hardware qualification on the named board remains
  pending.
- The Linux platform is a native hosted lane. It builds and runs on the
  architecture it names; it does not cross-compile. The SDL2 backend requires
  the SDL2 development libraries at build time.
- `mm.stdio` on the Pico SDK uses USB CDC and requires the Pico SDK's USB
  stack. A board without USB CDC must provide its own `mm.stdio` provider.

### Compatibility

- `mm: 1.0`, `mm: 1.1`, and `mm: 1.2` manifests are unchanged and still valid.
- No new manifest keys are introduced; all new modules use existing `kind:`,
  `module:`, `platform-interface:`, `platform-provider:`, `use:`, and
  `file:` keys under `mm: 1.2`.
- The configuration record stays `configuration-2`.
- GCC 15 or newer is required, as in v1.2.0.

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

[v1.2.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.1
[v1.2.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.2.0
[v1.1.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.1.0
[v1.0.1]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.1
[v1.0.0]: https://github.com/modules-cpp/modules.cpp/releases/tag/v1.0.0
