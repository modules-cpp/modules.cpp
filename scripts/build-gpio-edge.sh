#!/bin/sh
# apps/gpio-edge-smoke on a Pico board: the edge-latch adapter symbols must be
# in the image. Physical edge delivery needs a wired board run: GPIO14 joined
# to GPIO15. The Waveshare GEEK boards, rp2040_geek and rp2350_geek, build it,
# but neither brings GPIO14 or GPIO15 out -- their only exposed GPIOs are GP2
# and GP3, GP4 and GP5, and GP28 and GP29 on the three headers -- so on them
# the build is the whole check. The Waveshare Zero boards, rp2040_zero and
# rp2350_zero, have GPIO14 and GPIO15 on header pins 15 and 16, so the wired
# run works on them as on a Pico.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-gpio-edge
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=gpio-edge-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_gpio_watch --abi mm_pico_mcu_gpio_wait"
wrapper_linux=""
wrapper_usage="apps/gpio-edge-smoke: the GPIO edge latch on a Pico board
GEEK boards: --board rp2040_geek or --board rp2350_geek (build only)
Zero boards: --board rp2040_zero or --board rp2350_zero"

wrapper_after() {
    case "$board" in
        rp2040_geek|rp2350_geek)
            echo "Hardware check: none on $board; GPIO14 and GPIO15 reach no header,"
            echo "  so the GPIO14 to GPIO15 jumper cannot be fitted."
            return
            ;;
    esac
    echo "Hardware check: join GPIO14 (driver) to GPIO15 (sensor), then flash"
    echo "  apps/gpio-edge-smoke/ and open its USB CDC console. Remove the jumper"
    echo "  when asked, and replace it after the manual step."
}

wrapper_main "$@"
