#!/bin/sh
# Build apps/font-demo for a native Linux lane on the emulated e-paper board,
# and verify what came out.
#
# The e-paper board rebinds two interfaces: mm.display to the real SSD1680
# controller, and mm.mcu to the emulated chip behind the seam the controller
# talks through. font-demo names both, so the image must carry one object for
# each, the ssd1680 driver between them, and the emulated chip and its SDL2
# window behind the MCU provider. No DRM, evdev, touch, IMU or RTC object may
# reach the link: the board inherits those bindings, and the demo never
# mentions them.
#
# The panel is the 152 by 296 tri-color one the board declares, one bit deep,
# so this is the lane where the demo's packed-row path is the one exercised,
# not the RGB565 expansion the sixteen-bit panels take.
#
# --run is expected to work on an ordinary desktop. The emulation opens a
# window when the controller's refresh completes, so no virtual terminal is
# needed.
set -eu

test_name=build-linux-epaper-font-demo
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
mm_enter_root

app=font-demo
app_path=apps/font-demo
run_app=no
compiler=
run_must_succeed=yes
arch=

while [ "$#" -gt 0 ]; do
    case "$1" in
        -a|--arch)
            mm_option_value "$#" "$1" "an architecture"
            arch=$2
            shift 2
            ;;
        -c|--compiler)
            mm_option_value "$#" "$1" "a compiler"
            compiler=$2
            shift 2
            ;;
        --run)
            run_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [-a|--arch aarch64|x86_64] [-c|--compiler CXX] [--run]"
            echo "architectures: aarch64, x86_64 (default: this machine's)"
            echo "--compiler defaults to <triple>-g++; name another to avoid a broken one"
            echo "--run opens the emulation window; no virtual terminal is needed"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

mm_linux_resolve_arch
mm_linux_lane epaper
inherited_map=$linux_map
mm_linux_prepare
mm_linux_require_sdl2
mm_require_host_tools
mm_trap_restore_host

binary="out-target-$target/$app_path/$app"

# The exit codes are the steps of apps/font-demo/main.cpp, in order.
explain_run() {
    case "$1" in
        0) echo "  four lines and the whole charset were drawn and held in four" ;
           echo "  orientations, in the emulation window" ;;
        1) echo "  1 is display.initialize: the controller's reset or busy" ;
           echo "  handshake with the emulated chip failed" ;;
        2) echo "  2 is the geometry check: the panel is neither one nor" ;
           echo "  sixteen bits deep, or reported no size" ;;
        3) echo "  3 is the frame budget: the panel is wider or narrower" ;
           echo "  than the demo's static frame allows" ;;
        4) echo "  4 is display.clear" ;;
        5) echo "  5 is fonts.render: a line did not compose" ;;
        6) echo "  6 is gfx.write: the controller refused the packed frame" ;;
        7) echo "  7 is display.refresh: the emulated refresh did not complete," ;
           echo "  or the window could not be shown" ;;
        8) echo "  8 is mcu.delay_ms: the hold did not complete" ;;
        9) echo "  9 is display.sleep" ;;
        *) echo "  see apps/font-demo/main.cpp for that step" ;;
    esac
}

echo "Native Linux lane, emulated e-paper"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    $board"
echo "  app      $app"
echo "  sdl2     $(pkg-config --modversion sdl2 2>/dev/null || echo 'headers present')"

echo
echo "E-paper board"

mm_linux_configure "$board"

./build --target "$app_path/"

mm_linux_verify_image "$binary"
mm_linux_verify_sdl2 "$binary" yes \
    "the emulated device's window did not reach the link line"
# The real controller the display provider wraps, and both font tables the
# demo draws with.
mm_verify_symbol "$binary" mm::epaper::ssd1680
mm_verify_symbol "$binary" mm::fonts::kMono12Data
mm_verify_symbol "$binary" mm::fonts::kMono16Data

# The two interfaces font-demo reaches, both rebound by the board, the chip
# that arrives behind them, and the six providers the SDK binds that nothing
# here mentions. The named device map is absent too: the DRM and evdev
# providers are what use it, and the emulated device needs no device names.
mm_verify_provider "$binary" platform.linux.epaper.display 1
mm_verify_provider "$binary" platform.linux.epaper.device 1
mm_verify_provider "$binary" platform.linux.epaper.chip 1
mm_verify_provider "$binary" "$inherited_map" 0
mm_verify_provider "$binary" platform.linux.display 0
mm_verify_provider "$binary" platform.linux.mcu 0
mm_verify_provider "$binary" platform.linux.sdl 0
mm_verify_provider "$binary" platform.linux.touch 0
mm_verify_provider "$binary" platform.linux.imu 0
mm_verify_provider "$binary" platform.linux.rtc 0
mm_verify_provider "$binary" platform.linux.stdio 0
mm_verify_provider "$binary" platform.linux.defaults 0

echo "  emulated chip and controller, both font tables, SDL2 on the link line,"
echo "  nothing else"

if [ "$run_app" = yes ]; then
    mm_linux_run
fi

mm_leave_host

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-linux-epaper-font-demo.sh --run"
echo "A 152 by 296 window opens when the first refresh completes: four centred"
echo "lines, two at 16px and two at 12px, the last one Polish, then every glyph"
echo "of the 12px table, black on white. Upright first, then a quarter turn"
echo "clockwise on each of three more refreshes, four seconds each."
