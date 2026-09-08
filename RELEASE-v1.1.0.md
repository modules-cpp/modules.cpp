# modules.cpp v1.1.0

Cross compilation. v1.0 built, tested and documented itself with one compiler.
v1.1 adds `configure`, which selects a compiler, a build type and a target, and
`run` and `debug`, which execute and debug target binaries under an emulator.

```sh
./configure --compiler gcc-15 --build release
./build && ./test

./configure --target m68k-linux-gnu --compiler m68k-linux-gnu-g++-16 \
            --runner qemu-user --debugger gdb --build release
./build --target
./run --target apps/main
```

Host and target output stay separate: `out-host/` and `out-target-<triple>/`,
with bootstrap output and installed commands still in `out/`. A cross-compiled
executable never replaces a host tool.

Manifests can declare typed options that inherit down the folder tree and can be
locked, including `buildable-host` and `buildable-target`, which are validated
across `use:` edges so a portable application cannot depend on a host-only
module.

**Hosted cross targets work end to end; freestanding targets compile but cannot
link.** No tool converts an option into a compiler or linker argument in this
release, and `configure` is the only route for compiler, target, build type and
flags. A bare-metal target such as `arm-none-eabi` needs `--specs=` and syscall
stubs, and there is no route for either yet.

`mm: 1.0` manifests are unchanged and still valid. `bootstrap.sh` still needs no
configuration.

Requires a C++20 compiler with module support, GCC 14 or newer recommended, and
a POSIX shell. See [CHANGELOG.md](CHANGELOG.md) for the full list and
`docs/modules-configure.mdy` for the specification.
