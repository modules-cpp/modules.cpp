#!/bin/sh
# apps/audio-smoke on any board that binds mm.audio: a second of tone through
# the board's Speaker, recorded through its Microphone where the board has one.
# On a Pico board the I2S adapter symbols must be in the image.
set -eu

test_name=build-audio
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=audio-smoke
wrapper_board=rp2350_touch_lcd_154
wrapper_both=""
wrapper_pico="--abi mm_pico_mcu_i2s_configure --abi mm_pico_mcu_i2s_write"
wrapper_linux=""
wrapper_usage="apps/audio-smoke: the board's speaker and microphone"

wrapper_after() {
    echo "A second of a 500 Hz square wave through the speaker. Its exit code names"
    echo "the step that failed: apps/audio-smoke/exit-codes."
}

wrapper_main "$@"
