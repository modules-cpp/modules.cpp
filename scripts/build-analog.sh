#!/bin/sh
# apps/analog-smoke on a Pico board: the ADC and PWM adapter symbols must be
# in the image, and target-smoke-any, which reaches no interface, must carry no
# provider. The analog path itself needs a wired board run: GP16 through 10
# kOhm to GP26, 1 uF from GP26 to ground, GP17 and GP0 unconnected.
set -eu

test_name=build-analog
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=analog-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_adc_configure --abi mm_pico_mcu_adc_read --abi mm_pico_mcu_pwm_configure --abi mm_pico_mcu_pwm_write"
wrapper_linux=""
wrapper_usage="apps/analog-smoke: ADC and PWM on a Pico board"

wrapper_after() {
    echo "Hardware check: GP16 through 10 kOhm to GP26, 1 uF from GP26 to ground,"
    echo "  GP17 and GP0 unconnected; flash apps/analog-smoke/ and open its USB CDC"
    echo "  console. Full exit-code table: apps/analog-smoke/mm.mdy"
}

wrapper_main "$@"
