#!/bin/sh
# m68k Linux with an external CMake library: the m68k-linux-external SDK,
# which links libraries/cmake-demo through its CMake bridge, and its
# cmake-demo-smoke application, run by qemu-user.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.
exec sh "$(dirname -- "$0")/build-target.sh" --target m68k-linux-gnu --sdk m68k-linux-external --target-host --runner qemu-user --app platforms/m68k-linux-external/apps/cmake-demo-smoke "$@"
