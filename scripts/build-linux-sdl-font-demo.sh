#!/bin/sh
# Build apps/font-demo for a native Linux lane on the SDL2 board, and verify
# what came out.
#
# font-demo names mm.fonts, mm.gfx, mm.display, and mm.mcu. The SDL
# board binds mm.display and mm.touch to one provider module, so the image must
# carry exactly one platform.linux.sdl object even though the application
# reaches only half of what it serves, and no DRM, evdev, IMU or RTC provider
# at all. mm.fonts is a plain module with no provider behind it: the tables and
# the renderer are linked like any other code, which is the point of it.
#
# --run is expected to work on an ordinary desktop. The SDL provider opens a
# window, so no virtual terminal and no DRM master are needed.
set -eu

test_name=build-linux-sdl-font-demo
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
            echo "--run opens a window; no virtual terminal is needed"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

mm_linux_resolve_arch
mm_linux_lane sdl
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
           echo "  orientations, in a window" ;;
        1) echo "  1 is display.initialize: a headless session or an" ;
           echo "  SDL_VIDEODRIVER override would explain this" ;;
        2) echo "  2 is the geometry check: the panel is neither one nor" ;
           echo "  sixteen bits deep, or reported no size" ;;
        3) echo "  3 is the frame budget: the panel is wider or narrower" ;
           echo "  than the demo's static frame allows" ;;
        4) echo "  4 is display.clear" ;;
        5) echo "  5 is fonts.render: a line did not compose" ;;
        6) echo "  6 is gfx.write: expansion or display.write failed" ;;
        7) echo "  7 is display.refresh" ;;
        8) echo "  8 is mcu.delay_ms: the hold did not complete" ;;
        9) echo "  9 is display.sleep" ;;
        *) echo "  see apps/font-demo/main.cpp for that step" ;;
    esac
}

echo "Native Linux lane, SDL2 backend"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    $board"
echo "  app      $app"
echo "  sdl2     $(pkg-config --modversion sdl2 2>/dev/null || echo 'headers present')"

echo
echo "SDL board"

mm_linux_configure "$board"

./build --target "$app_path/"

mm_linux_verify_image "$binary"
mm_linux_verify_sdl2 "$binary" yes
# The font tables are data the linker can drop only if nothing reaches them.
# Both faces are reached: the demo draws two lines in each.
mm_verify_symbol "$binary" mm::fonts::kMono12Data
mm_verify_symbol "$binary" mm::fonts::kMono16Data

# The two interfaces font-demo reaches, served by the board's one SDL object
# and the inherited MCU provider, and the four it never mentions.
mm_verify_provider "$binary" platform.linux.sdl 1
mm_verify_provider "$binary" platform.linux.mcu 1
mm_verify_provider "$binary" "$inherited_map" 1
mm_verify_provider "$binary" platform.linux.display 0
mm_verify_provider "$binary" platform.linux.touch 0
mm_verify_provider "$binary" platform.linux.imu 0
mm_verify_provider "$binary" platform.linux.rtc 0
mm_verify_provider "$binary" platform.linux.stdio 0
mm_verify_provider "$binary" platform.linux.defaults 0

echo "  SDL provider, both font tables, SDL2 on the link line, nothing else"

if [ "$run_app" = yes ]; then
    mm_linux_run
fi

mm_leave_host

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-linux-sdl-font-demo.sh --run"
echo "Four centred lines, two at 16px and two at 12px, the last one Polish,"
echo "then every glyph of the 12px table, wrapped to the window, upright and"
echo "then turned a quarter clockwise three times, four seconds each. The"
echo "second line is white on blue, the third red, the fourth green; a line"
echo "that comes out inverted means the set-bit colour went to the ink."
echo "Text that comes out scrambled means the packed rows and the panel's"
echo "row stride disagree."
