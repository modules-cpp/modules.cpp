#!/bin/sh
# apps/display-demo on any board with a display: three colour bars, rotating
# twice. Red is 0xf800; bars that come out blue mean a provider swapped the
# RGB565 bytes. On a generic Linux board the DRM provider needs DRM master, so
# --run belongs on a virtual terminal. On the Waveshare GEEK boards,
# rp2040_geek and rp2350_geek, the 240 by 135 landscape panel sits at an offset
# in the controller's frame memory, so bars clipped or shifted there point at
# the panel's column and row offsets.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-display
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=display-demo
wrapper_board=generic
wrapper_both=""
wrapper_pico=""
wrapper_linux="--control target-smoke-any"
wrapper_usage="apps/display-demo: three colour bars on the board's display
GEEK boards: --board rp2040_geek or --board rp2350_geek"

wrapper_after() {
    echo "Three colour bars, rotating twice. Red is 0xf800; bars that come out blue"
    echo "mean the provider swapped the RGB565 bytes."
}

wrapper_main "$@"
