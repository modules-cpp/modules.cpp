#!/bin/sh
# apps/stdio-smoke on any board: the console provider must be in the image and
# absent from target-smoke-any, which reaches no interface. On a Pico that is
# the USB CDC console; a build does not prove a host received the bytes.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-stdio
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=stdio-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/stdio-smoke: the portable console"

wrapper_after() {
    echo "Hardware check: flash apps/stdio-smoke/, open the USB CDC terminal, then"
    echo "  reset the board. Expected output: modules.cpp mm.stdio over USB CDC"
}

wrapper_main "$@"
