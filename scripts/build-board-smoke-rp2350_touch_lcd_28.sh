#!/bin/sh
# Build apps/board-smoke for the Waveshare RP2350-Touch-LCD-2.8 through the
# Pico SDK bridge, and verify what came out.
#
# The board binds five platform interfaces, and board-smoke reaches four of
# them, so this is the one build that proves provider injection resolves more
# than one: mm.display, mm.touch, mm.imu, and mm.rtc must each contribute
# exactly one registration to the image, alongside the MCU provider, and the
# audio provider none.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not run it. A successful build is
# not a claim that the panel lit up.
set -eu

test_name=build-board-smoke-rp2350_touch_lcd_28
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/pico.sh"
mm_enter_root

board=rp2350_touch_lcd_28
app=board-smoke
app_path=apps/board-smoke

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            echo "usage: $0"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

mm_load_board "$board"
mm_pico_lane "$board"
mm_pico_prepare
mm_trap_restore_host

mm_pico_banner
echo "  board  $board"
echo "  app    $app"

mm_pico_configure
mm_pico_build "$app_path/"

binary="out-target-$target/$app_path/$app"
mm_pico_verify_image "$binary"

# The interfaces the application reaches, each served once with its driver
# behind it, and every other interface the board binds, absent.
mm_verify_board_providers "$binary" "display touch imu rtc"
mm_verify_provider "$binary" platform.pico.mcu 1

mm_pico_verify_uf2 "$binary"

mm_leave_host

echo "PASS: $test_name"
