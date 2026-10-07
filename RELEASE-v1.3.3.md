# modules.cpp v1.3.3

Something to build beyond the checkout with. v1.3.3 lets an application
keep its manifest portable while configure records the chosen modules.cpp
installation and toolchain locally. Build, run, flash, debug, and clean then
use that binding across standalone and multi-app projects.

```sh
./build.sh
./test.sh
# From a separate directory containing mm.mdy and application sources:
/path/to/modules.cpp/configure.sh --host --compiler clang++
/path/to/modules.cpp/build.sh --host
/path/to/modules.cpp/run.sh --host
```

An external application's source manifest stays on the supported `mm: 1.3`
schema and does not name a particular modules.cpp installation. Configure
finds the installed tool root and writes the machine-local binding to the
external project's `out/config.mdy`. Consumers use that saved binding and
reject stale or incomplete build outputs. Multiple ordinary C++ source files
can form an application, and connected `kind: dir` projects share one
configuration tree. A native Linux target can infer host compiler defaults
from its selected native compiler.

`sketch --external` generates a locator-free sketch application. Existing
`project:` sketches keep their established workflow. The installed clean tool
can remove owned external build lanes without losing configuration, or reset
an owned external tree with `--distclean`. The shell regression suite now
covers wrappers, release-fetch diagnostics, standalone UF2 argument handling,
external application lifecycles, and native provider and sketch builds.

**External named C++20 module declarations are not supported.** Use installed
module interfaces from ordinary application translation units. **Optional
Pico validation builds firmware and uses a recording flash backend; it does
not establish operation on hardware.** The v1.3.2 camera hardware observation
remains documented separately.

Source manifests `mm: 1.0` through `1.3` remain supported. Managed external
applications generate a local configuration record using schema 3.0; source
manifests do not require version 1.4.

Requires a C++20 compiler with module support and the development packages
for the chosen host providers. External Linux integration tests require GCC
and Clang. Pico builds require the Arm toolchain, Pico SDK, picotool, and
its tools. See [CHANGELOG.md](CHANGELOG.md),
`docs/modules-external-apps.mdy`, `docs/modules-clean.mdy`,
`docs/modules-configure.mdy`, and `docs/modules-sketch.mdy`.
