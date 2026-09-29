#!/bin/sh
# Build apps/font-demo for the Waveshare RP2350-Touch-LCD-2.8 through the Pico
# SDK bridge, and verify what came out.
#
# The board binds five platform interfaces, but font-demo names only
# mm.display and mm.mcu, so this build proves provider injection follows the
# application's closure and not the board's: exactly one display provider and
# one MCU provider, no touch, IMU, RTC or audio object, and the ST7789 driver behind
# the display. The font tables are plain data that the linker keeps because the
# demo draws with both.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not flash or run it. --flash hands
# the built application to ./flash.sh, which needs the board in BOOTSEL mode.
set -eu

test_name=build-font-demo-rp2350_touch_lcd_28
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/pico.sh"
mm_enter_root

board=rp2350_touch_lcd_28
app=font-demo
app_path=apps/font-demo
flash_app=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        --flash)
            flash_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [--flash]"
            echo "--flash writes the image with picotool; put the board in BOOTSEL first"
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
mm_verify_board_providers "$binary" "display"
mm_verify_provider "$binary" platform.pico.mcu 1

# Both font tables: the demo draws two lines in each, so neither may be
# garbage-collected out of the image.
mm_verify_symbol "$binary" mm::fonts::kMono12Data
mm_verify_symbol "$binary" mm::fonts::kMono16Data

mm_pico_verify_uf2 "$binary"

echo "  display and mcu providers, st7789 driver, both font tables, nothing else"

if [ "$flash_app" = yes ]; then
    mm_pico_flash "$app_path/"
fi

mm_leave_host

echo "PASS: $test_name"
echo "To see it: hold BOOTSEL, plug the board in, then"
echo "  scripts/build-font-demo-rp2350_touch_lcd_28.sh --flash"
echo "Four centred lines, two at 16px and two at 12px, the last one Polish,"
echo "then every glyph of the 12px table, upright and then turned a quarter"
echo "clockwise three times, four seconds each. The second line is white on"
echo "blue, the third red, the fourth green; blue that comes out red means the"
echo "RGB565 byte swap was dropped."
