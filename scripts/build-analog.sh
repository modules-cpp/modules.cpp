#!/bin/sh
# apps/analog-smoke on a Pico board: the ADC and PWM adapter symbols must be
# in the image, and target-smoke-any, which reaches no interface, must carry no
# provider. The analog path itself needs a wired board run: GP16 through 10
# kOhm to GP26, 1 uF from GP26 to ground, GP17 and GP0 unconnected. The
# Waveshare Zero boards, rp2040_zero and rp2350_zero, build it, but their GP16
# drives the WS2812B and reaches no pin, so on them the build is the whole
# check. So it is on the Waveshare GEEK boards, rp2040_geek and rp2350_geek,
# whose GP16 and GP26 reach no header.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-analog
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=analog-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_adc_configure --abi mm_pico_mcu_adc_read --abi mm_pico_mcu_pwm_configure --abi mm_pico_mcu_pwm_write"
wrapper_linux=""
wrapper_usage="apps/analog-smoke: ADC and PWM on a Pico board
GEEK boards: --board rp2040_geek or --board rp2350_geek (build only)
Zero boards: --board rp2040_zero or --board rp2350_zero (build only)"

wrapper_after() {
    case "$board" in
        rp2040_zero|rp2350_zero)
            echo "Hardware check: none on $board; GP16 drives the WS2812B and"
            echo "  reaches no pin, so the GP16 to GP26 fixture cannot be wired."
            return
            ;;
        rp2040_geek|rp2350_geek)
            echo "Hardware check: none on $board; GP16 and GP26 reach no header,"
            echo "  so the GP16 to GP26 fixture cannot be wired."
            return
            ;;
    esac
    echo "Hardware check: GP16 through 10 kOhm to GP26, 1 uF from GP26 to ground,"
    echo "  GP17 and GP0 unconnected; flash apps/analog-smoke/ and open its USB CDC"
    echo "  console. Full exit-code table: apps/analog-smoke/mm.mdy"
}

wrapper_main "$@"
