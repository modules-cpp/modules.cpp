#!/bin/sh
# 32-bit Arm Linux, hard float: the arm-linux-gnueabihf-glibc cross SDK, with
# its runtime under /usr/arm-linux-gnueabihf. It binds no platform provider,
# so only applications that reach no platform interface build here; --run
# uses qemu-arm.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.
exec sh "$(dirname -- "$0")/build-target.sh" --target arm-linux-gnueabihf --sdk arm-linux-gnueabihf-glibc --target-host --app target-smoke-any "$@"
