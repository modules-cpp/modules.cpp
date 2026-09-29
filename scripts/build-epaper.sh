#!/bin/sh
# apps/gfx-demo or apps/font-demo on an e-paper board: a Pico with a
# Waveshare Pico-ePaper-2.66, black and white or with red, or the emulated
# e-paper board on Linux. The panel is one bit deep, so this is where the demos'
# packed-row path runs; the SSD1680 driver must be in the image.
set -eu

test_name=build-epaper
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

# --app gfx|font chooses the demo and --panel bw|bwr|b the Pico-ePaper-2.66
# or its -B; on Linux the emulated e-paper board is --board epaper.
wrapper_app=gfx-demo
kind=gfx
panel=
set -- "$@" --end-of-arguments
while [ "$1" != --end-of-arguments ]; do
    case "$1" in
        --app)
            if [ "$2" = --end-of-arguments ]; then
                echo "$test_name: $1 requires gfx or font" >&2
                exit 64
            fi
            kind=$2
            shift 2
            continue
            ;;
        --panel)
            if [ "$2" = --end-of-arguments ]; then
                echo "$test_name: $1 requires bw, bwr, or b" >&2
                exit 64
            fi
            panel=$2
            shift 2
            continue
            ;;
    esac
    set -- "$@" "$1"
    shift
done
shift
case "$kind" in
    gfx) wrapper_app=gfx-demo; wrapper_both="--no-symbol mm::fonts" ;;
    font) wrapper_app=font-demo
          wrapper_both="--symbol mm::fonts::kMono12Data --symbol mm::fonts::kMono16Data" ;;
    *) echo "$test_name: unknown demo: $kind (expected gfx or font)" >&2; exit 64 ;;
esac
case "$panel" in
    ''|bw) wrapper_board=pico_epaper ;;
    bwr|b) wrapper_board=pico_epaper_b ;;
    *) echo "$test_name: unknown panel: $panel (expected bw, bwr, or b)" >&2; exit 64 ;;
esac
epaper_both=$wrapper_both
wrapper_app=${wrapper_app:-gfx-demo}
wrapper_board=${wrapper_board:-pico_epaper}
wrapper_both="$epaper_both"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/gfx-demo or apps/font-demo on an e-paper board; --app gfx|font, --panel bw|bwr"

wrapper_after() {
    echo "Four full refreshes, four seconds apart, black on white, turned a quarter"
    echo "clockwise each time, before the panel sleeps. On Linux the window opens"
    echo "when the first refresh completes."
}

wrapper_main "$@"
