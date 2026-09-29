#!/bin/sh
# Build apps/board-smoke for a native Linux lane, and verify what came out.
#
# This is the portability proof. apps/board-smoke names four interfaces and no
# board, no SDK, and no vendor, and scripts/build-board-smoke-rp2350_touch_lcd_28.sh
# builds the same source for a Waveshare RP2350-Touch-LCD-2.8, where those four
# resolve to an ST7789, a CST328, a QMI8658, and a PCF85063 over SPI and I2C.
# Here they resolve to DRM, evdev, IIO, and /dev/rtc. One application, two
# platforms, no shared provider.
#
# What this script asserts that scripts/build-linux-smoke.sh cannot: injection
# is driven by the application's closure, not by what the SDK declares. The
# Linux SDK binds seven interfaces; board-smoke reaches four of them, so the MCU
# and console providers must be absent from the image even though the selected
# platform binds both. linux-smoke uses all six device interfaces and so can
# never show that.
#
# The map provider is the fifth. Nothing in board-smoke mentions it: the four
# device providers each use platform.linux.map, and provider requirements are
# expanded to a fixed point, so binding a display drags in the map behind it.
#
# The lane is the target lane aimed at the build machine's own triple. A target
# lane means "not the host compiler", and aarch64-linux-gnu-g++ is a different
# driver from g++ even on an aarch64 machine.
#
# This builds and inspects an executable; it does not judge one by running it.
# board-smoke returns at the first interface that is present and fails, and in
# an ordinary desktop session the compositor holds DRM master, so a run is
# reported and never gates. Use --run to see it.
set -eu

test_name=build-linux-board-smoke
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
mm_enter_root

app=board-smoke
app_path=apps/board-smoke
control_app=target-smoke-any
run_app=no
compiler=
run_must_succeed=no
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
            echo "--run needs all four devices; a desktop session has none of them"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

mm_linux_resolve_arch

# Only the machine's own architecture can be built here: the native runner is
# compatible with one triple, and an executable for another machine could not
# be inspected for the properties below with any meaning.
mm_linux_lane generic
board_map=$linux_map
mm_linux_prepare
mm_require_host_tools
mm_trap_restore_host

binary="out-target-$target/$app_path/$app"
control_binary="out-target-$target/apps/$control_app/$control_app"

explain_run() {
    case "$1" in
        0) echo "  all four interfaces initialised and answered;" ;
           echo "  board-smoke has no console, so a white frame is its only output" ;;
        2) echo "  2 is display.initialize: a compositor holds DRM master" ;;
        4) echo "  4 is touch.initialize: /dev/input needs the input group" ;;
        5) echo "  5 is imu.initialize: this machine exposes no IIO device" ;;
        *) echo "  see apps/board-smoke/main.cpp for that step" ;;
    esac
}

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  app      $app"

# The four interfaces board-smoke names, and the two the SDK also binds but
# which nothing in this application reaches. The absences are the assertion:
# they are what distinguishes closure-driven injection from linking whatever
# the platform happens to declare.
verify_reached_providers() {
    image=$1
    mm_verify_provider "$image" platform.linux.display 1
    mm_verify_provider "$image" platform.linux.touch 1
    mm_verify_provider "$image" platform.linux.imu 1
    mm_verify_provider "$image" platform.linux.rtc 1
    mm_verify_provider "$image" platform.linux.mcu 0
    mm_verify_provider "$image" platform.linux.stdio 0
}

echo
echo "SDK alone"

mm_linux_configure

./build --target "$app_path/"

mm_linux_verify_image "$binary"
verify_reached_providers "$binary"

# The map arrives behind the four device providers rather than from the
# application, which names it nowhere.
mm_verify_provider "$binary" platform.linux.defaults 1
if [ -n "$board_map" ]; then
    mm_verify_provider "$binary" "$board_map" 0
fi

echo "  four device providers, map from the SDK, no mcu or stdio"

if [ -n "$board" ]; then
    echo
    echo "Board over SDK"

    mm_linux_configure "$board"

    ./build --target "$app_path/"

    mm_linux_verify_image "$binary"
    verify_reached_providers "$binary"

    # The board replaces exactly one binding, and the interface it replaces is
    # one the application never mentions. A generic map still present would mean
    # both providers were linked and their registrations raced; the board's map
    # absent would mean the override did not take.
    mm_verify_provider "$binary" "$board_map" 1
    mm_verify_provider "$binary" platform.linux.defaults 0

    echo "  four device providers, map from the board, no mcu or stdio"
else
    echo
    echo "Board over SDK: skipped, no board is defined for $arch"
fi

# Provider injection follows the application's closure. A portable application
# that reaches none of these interfaces must not pay for any of their providers.
echo
echo "Control"

./build --target "apps/$control_app/"
mm_linux_verify_image "$control_binary"
for provider in \
    platform.linux.display \
    platform.linux.touch \
    platform.linux.imu \
    platform.linux.rtc \
    platform.linux.mcu \
    platform.linux.stdio \
    platform.linux.defaults; do
    mm_verify_provider "$control_binary" "$provider" 0
done
echo "  no provider objects in $control_app"

if [ "$run_app" = yes ]; then
    mm_linux_run
fi

mm_leave_host

echo
echo "PASS: $test_name"
echo "The same source builds for the RP2350-Touch-LCD-2.8 through"
echo "scripts/build-board-smoke-rp2350_touch_lcd_28.sh, with four entirely"
echo "different providers behind the same four interfaces."
echo "Full qualification: run from a virtual terminal, with membership of the"
echo "video and input groups, where DRM master is available."
