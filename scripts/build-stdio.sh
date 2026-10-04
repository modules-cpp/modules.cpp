#!/bin/sh
# apps/stdio-smoke on any board: the console provider must be in the image and
# absent from target-smoke-any, which reaches no interface. On a Pico that is
# the USB CDC console; a build does not prove a host received the bytes. On
# the Waveshare GEEK boards, rp2040_geek and rp2350_geek, the native port is
# the stick's USB-A plug, so the console appears once the board is plugged
# straight into the host; the UART header is not the console. The Waveshare
# Zero boards, rp2040_zero and rp2350_zero, give the console on their USB-C
# socket.

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
wrapper_usage="apps/stdio-smoke: the portable console
GEEK boards: --board rp2040_geek or --board rp2350_geek
Zero boards: --board rp2040_zero or --board rp2350_zero"

wrapper_after() {
    echo "Hardware check: flash apps/stdio-smoke/, open the USB CDC terminal, then"
    echo "  reset the board. Expected output: modules.cpp mm.stdio over USB CDC"
}

wrapper_main "$@"
