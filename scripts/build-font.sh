#!/bin/sh
# apps/font-demo on any board with a display: four centred lines, two at 16px
# and two at 12px, the last one Polish, then every glyph of the 12px table, in
# four orientations. Both font tables must reach the image: the demo draws two
# lines in each, so neither may be garbage-collected out.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-font
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=font-demo
wrapper_board=rp2350_touch_lcd_28
wrapper_both="--symbol mm::fonts::kMono12Data --symbol mm::fonts::kMono16Data"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/font-demo: both font tables on the board's display"

wrapper_after() {
    echo "Four centred lines, two at 16px and two at 12px, the last one Polish,"
    echo "then every glyph of the 12px table, upright and then turned a quarter"
    echo "clockwise three times. On a colour panel blue that comes out red means"
    echo "the RGB565 byte swap was dropped."
}

wrapper_main "$@"
