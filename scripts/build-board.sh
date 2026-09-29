#!/bin/sh
# apps/board-smoke on any board that binds a display, touch, an IMU, and a
# clock: the one build that proves provider injection resolves four interfaces
# at once, from one portable source, whatever providers stand behind them.
set -eu

test_name=build-board
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=board-smoke
wrapper_board=rp2350_touch_lcd_28
wrapper_both="--control target-smoke-any"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/board-smoke: display, touch, IMU, and clock together"

wrapper_after() {
    echo "board-smoke has no console: a white frame after the black one is its only"
    echo "output. On Linux, full qualification is a run from a virtual terminal with"
    echo "membership of the video and input groups."
}

wrapper_main "$@"
