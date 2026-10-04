#!/bin/sh
# apps/rgb-led-smoke on any board that binds mm.led: red, green, blue, white,
# and a colour wheel, then dark. On a Pico board the pulse adapter symbols must
# be in the image, and target-smoke-any, which reaches no interface, must
# carry no provider. The Waveshare Zero boards, rp2040_zero and rp2350_zero,
# carry one WS2812B on GP16.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-rgb-led
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=rgb-led-smoke
wrapper_board=rp2040_zero
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_pulse_configure --abi mm_pico_mcu_pulse_write --abi mm_pico_mcu_pulse_release"
wrapper_linux=""
wrapper_usage="apps/rgb-led-smoke: the board's RGB LEDs through mm.led
Zero boards: --board rp2040_zero or --board rp2350_zero"

wrapper_after() {
    echo "Hardware check: flash apps/rgb-led-smoke/ and watch the LED: red, green,"
    echo "  blue, and white for a second each, a colour wheel, then dark. Red and"
    echo "  green swapped mean the controller's byte order is wrong."
}

wrapper_main "$@"
