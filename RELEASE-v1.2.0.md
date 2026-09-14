# modules.cpp v1.2.0

Targets that are real hardware. v1.1 could cross compile and run a hosted target
under an emulator, but a bare-metal target compiled and could not link. v1.2 adds
the three things that were missing — a description of the platform, a way to
consume code the project does not own, and a portable interface to the machine —
so one manifest tree now produces host tools, emulator targets, and firmware that
runs on a Raspberry Pi Pico.

```sh
./configure --target arm-none-eabi --compiler arm-none-eabi-gcc \
            --sdk pico-arm --board pico --build debug
./build --target apps/mcu-blink/
./flash apps/mcu-blink/
```

`kind: sdk` and `kind: board` manifests describe a target: triple, compiler
family, runtime, processor properties, linker script, board sources, and machine.
Five startup responsibilities — reset vector, initial stack, memory layout,
runtime init, syscalls — are declared rather than assumed, and a bare-metal lane
that leaves one unowned is rejected before the linker runs.

`kind: library` describes a vendored tree and its public include interface, and
`external-build: cmake` hands the final link to a project-owned bridge beside the
library manifest. The Pico SDK is integrated that way: build generates the
bridge's inputs, checks that the bridge and the project agree on the target ABI,
and publishes the ELF and a UF2 that real picotool accepts. ARM and RISC-V are
both supported, on six Pico boards.

An application says `use: mm.mcu` and names no board, SDK, or vendor. A selected
platform binds that interface to an implementation with `platform-provider:`, and
the build injects the provider only into executables whose dependency closure
actually reaches the interface — so an unrelated application in the same lane
receives no provider object and no static initialiser. `mm.display` and an
SSD1680 ePaper controller follow the same shape.

`derives-from:` builds one board on another, inheriting its platform identity and
changing only what differs: the memory map, extra sources, the machine, or the
provider binding. Custom hardware lives in `boards/`, separate from the reference
support in `platforms/`, and needs no processor registry entry of its own.

`option: core` with `read-only: core` marks a branch that may not reach
third-party code, machine-checked across `use:` edges. A core application stays
core while running on a platform whose provider is not.

**Nothing automated asserts that a Pico image ran.** Hardware execution is an
opt-in script whose result a person reads. RP2350 RISC-V hardware has not been
exercised. Tuning options other than `buildable-host`, `buildable-target` and
`core` are still validated and recorded but not applied to compilation, and
builds remain full rather than incremental.

`mm: 1.0` and `mm: 1.1` manifests are unchanged and still valid. Every platform,
library, provider, and derivation key requires `mm: 1.2`, so a tool that predates
this release rejects such a manifest by version rather than misreading a key.

Requires a C++20 compiler with module support, GCC 14 or newer recommended, and a
POSIX shell. Cross targets additionally require their toolchain; the Pico
platforms require the prebuilt tools that
`platforms/pico/install-sdk-tools.sh` provisions. See
[CHANGELOG.md](CHANGELOG.md) for the full list, `docs/modules-platforms.mdy` for
the platform specification, and `docs/modules-libraries.mdy` and
`docs/modules-cmake.mdy` for external builds.
