#!/bin/sh
# Build apps/gfx-demo for a Raspberry Pi Pico with a Waveshare Pico-ePaper-2.66
# through the Pico SDK bridge, and verify what came out.
#
# Two boards carry that panel: pico_epaper is the black and white one, and
# pico_epaper_b the black, white and red one. Both bind mm.display to a
# board-owned provider over the SSD1680 driver, and mm.mcu to the Pico SDK's.
# gfx-demo names exactly those two interfaces, so the image must carry one
# object for each and the driver between them, and nothing for the interfaces
# the demo never mentions.
#
# The panel is one bit deep, so this is the lane where the demo's packed-row
# path runs on real hardware.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not flash or run it. --flash hands
# the built application to ./flash.sh, which needs the board in BOOTSEL mode.
set -eu

test_name=build-gfx-demo-pico-epaper
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/pico.sh"
mm_enter_root

app=gfx-demo
app_path=apps/gfx-demo
panel=bw
flash_app=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        --panel)
            mm_option_value "$#" "$1" "bw, bwr, or b"
            panel=$2
            shift 2
            ;;
        --flash)
            flash_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [--panel bw|bwr|b] [--flash]"
            echo "--panel bw is the Pico-ePaper-2.66 (default); bwr or b the -B"
            echo "--flash writes the image with picotool; put the board in BOOTSEL first"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

case "$panel" in
    bw)
        board=pico_epaper
        other_board=pico_epaper_b
        ;;
    bwr|b)
        board=pico_epaper_b
        other_board=pico_epaper
        ;;
    *)
        echo "$test_name: unknown panel: $panel (expected bw, bwr, or b)" >&2
        exit 64
        ;;
esac

mm_load_board "$board"
mm_pico_lane "$board"
mm_pico_prepare
mm_trap_restore_host

mm_pico_banner
echo "  panel  $panel"
echo "  board  $board"
echo "  app    $app"

mm_pico_configure
mm_pico_build "$app_path/"

binary="out-target-$target/$app_path/$app"
mm_pico_verify_image "$binary"

# The two interfaces the demo reaches with the SSD1680 driver behind the
# display, and the other panel's provider, which must not be here: two boards
# share one driver, and only the selected one may register.
mm_verify_board_providers "$binary" "display"
mm_verify_provider "$binary" platform.pico.mcu 1
mm_verify_provider "$binary" "platform.$other_board.display" 0
mm_verify_no_symbol "$binary" mm::fonts

# Both e-paper boards sit on the original Pico.
mm_pico_verify_uf2 "$binary"

echo "  display and mcu providers, ssd1680 driver, nothing else"

if [ "$flash_app" = yes ]; then
    mm_pico_flash "$app_path/"
fi

mm_leave_host

echo "PASS: $test_name"
echo "To see it: hold BOOTSEL, plug the Pico in, then"
echo "  scripts/build-gfx-demo-pico-epaper.sh --panel $panel --flash"
echo "Four full refreshes, four seconds apart: a dithered glow around a"
echo "porthole full of rays, nested frames, and an off-centre tick, black on"
echo "white, turned a quarter clockwise each time, before the panel sleeps."
