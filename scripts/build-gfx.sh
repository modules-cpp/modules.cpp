#!/bin/sh
# apps/gfx-demo on any board with a display: a dithered glow, nested frames,
# a ringed porthole, and an off-centre tick, in four orientations. The image
# must carry no font tables: gfx draws without them. On the Waveshare GEEK
# boards, rp2040_geek and rp2350_geek, the panel is 240 by 135 in landscape,
# so the quarter turns lay the scene out 135 pixels wide, the narrowest of any
# panel under boards/.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-gfx
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=gfx-demo
wrapper_board=rp2350_touch_lcd_28
wrapper_both="--no-symbol mm::fonts"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/gfx-demo: packed-surface drawing on the board's display
GEEK boards: --board rp2040_geek or --board rp2350_geek"

wrapper_after() {
    echo "A dithered glow, nested frames, a ringed porthole, and an off-centre"
    echo "tick, in four orientations. On a colour panel six rainbow bands turn with"
    echo "the scene and a plasma swirls inside the porthole; bands that come out as"
    echo "solid blocks mean the expansion lost the bits, and a plasma in the wrong"
    echo "hues means the RGB565 byte swap was dropped."
}

wrapper_main "$@"
