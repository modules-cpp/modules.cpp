#!/bin/sh
# 64-bit Arm Linux: the Linux platform's linux-aarch64 SDK, whose providers
# serve mm.display, mm.touch, mm.mcu, and the rest over DRM, evdev, and the
# kernel's userspace ABI. --board generic-linux-aarch64, sdl-linux-aarch64, or
# epaper-linux-aarch64 adds a board; --sdk aarch64-linux-glibc selects the
# bare glibc cross SDK instead, which needs /usr/aarch64-linux-gnu.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

exec sh "$(dirname -- "$0")/build-target.sh" --target aarch64-linux-gnu --sdk linux-aarch64 --target-host --app target-smoke-any "$@"
