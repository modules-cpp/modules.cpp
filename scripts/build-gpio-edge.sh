#!/bin/sh
# apps/gpio-edge-smoke on a Pico board: the edge-latch adapter symbols must be
# in the image. Physical edge delivery needs a wired board run: GPIO14 joined
# to GPIO15.
set -eu

test_name=build-gpio-edge
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=gpio-edge-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_gpio_watch --abi mm_pico_mcu_gpio_wait"
wrapper_linux=""
wrapper_usage="apps/gpio-edge-smoke: the GPIO edge latch on a Pico board"

wrapper_after() {
    echo "Hardware check: join GPIO14 (driver) to GPIO15 (sensor), then flash"
    echo "  apps/gpio-edge-smoke/ and open its USB CDC console. Remove the jumper"
    echo "  when asked, and replace it after the manual step."
}

wrapper_main "$@"
