#!/bin/sh
# Bare-metal Arm: the mps2-an385 board over the arm-none-eabi-newlib SDK, run
# by qemu-system with semihosting. --sdk arm-none-eabi-newlib builds without
# the board, which leaves nothing to run. The Pico boards share this triple
# and are built by scripts/build-pico.sh.
#
# Every option passes through to scripts/build-target.sh, a later --app,
# --sdk, --board, --compiler, or --runner replacing the one given here; see
# its --help.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

exec sh "$(dirname -- "$0")/build-target.sh" --target arm-none-eabi --board mps2-an385 --runner qemu-system --app platforms/mps2-an385/target-smoke "$@"
