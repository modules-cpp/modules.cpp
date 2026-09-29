#!/bin/sh
# x86-64 Linux: the Linux platform's linux-x86_64 SDK. --board
# generic-linux-x86_64, sdl-linux-x86_64, or epaper-linux-x86_64 adds a board.
# On another architecture it needs the x86_64-linux-gnu cross toolchain, and
# --run needs qemu-x86_64.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.
exec sh "$(dirname -- "$0")/build-target.sh" --target x86_64-linux-gnu --sdk linux-x86_64 --target-host --app target-smoke-any "$@"
