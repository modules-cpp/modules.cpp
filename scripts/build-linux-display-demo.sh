#!/bin/sh
# Build apps/display-demo for a native Linux lane, and verify what came out.
#
# display-demo is the one application here whose output is a picture rather than
# an exit code, so this script is also the one whose --run is worth watching.
# Seeing it requires DRM master, and a compositor holds that on an ordinary
# desktop: switch to a virtual terminal first.
#
# It names two interfaces, mm.display and mm.mcu, which makes a third injection
# profile alongside the other two Linux scripts and is the point of building it
# separately:
#
#   linux-smoke        six device interfaces, every provider the SDK binds
#   board-smoke        four, so the MCU and console providers must be absent
#   display-demo       two, so touch, IMU, RTC and console must all be absent
#
# The map provider is the third object in all three. Nothing names it: the
# device providers each use platform.linux.map, and provider requirements are
# expanded to a fixed point, so binding a display drags the map in behind it.
#
# The lane is the target lane aimed at the build machine's own triple. A target
# lane means "not the host compiler", and aarch64-linux-gnu-g++ is a different
# driver from g++ even on an aarch64 machine.
set -eu

test_name=build-linux-display-demo
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
mm_enter_root

app=display-demo
app_path=apps/display-demo
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
            echo "--run needs DRM master: use a virtual terminal, not a desktop"
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
        0) echo "  three colour bars were drawn and held" ;;
        1) echo "  1 is display.initialize: a compositor holds DRM master," ;
           echo "  so run this from a virtual terminal" ;;
        2) echo "  2 is the geometry check: the provider reported none" ;;
        *) echo "  see apps/display-demo/main.cpp for that step" ;;
    esac
}

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  app      $app"

# The two interfaces display-demo names, and the four the SDK also binds but
# which nothing in this application reaches. The absences are the assertion.
verify_reached_providers() {
    image=$1
    mm_verify_provider "$image" platform.linux.display 1
    mm_verify_provider "$image" platform.linux.mcu 1
    mm_verify_provider "$image" platform.linux.touch 0
    mm_verify_provider "$image" platform.linux.imu 0
    mm_verify_provider "$image" platform.linux.rtc 0
    mm_verify_provider "$image" platform.linux.stdio 0
}

echo
echo "SDK alone"

mm_linux_configure

./build --target "$app_path/"

mm_linux_verify_image "$binary"
verify_reached_providers "$binary"

# The map arrives behind the display and MCU providers rather than from the
# application, which names it nowhere.
mm_verify_provider "$binary" platform.linux.defaults 1
if [ -n "$board_map" ]; then
    mm_verify_provider "$binary" "$board_map" 0
fi

echo "  display and mcu providers, map from the SDK, nothing else"

if [ -n "$board" ]; then
    echo
    echo "Board over SDK"

    mm_linux_configure "$board"

    ./build --target "$app_path/"

    mm_linux_verify_image "$binary"
    verify_reached_providers "$binary"

    # The board replaces exactly one binding, and the interface it replaces is
    # one the application never mentions.
    mm_verify_provider "$binary" "$board_map" 1
    mm_verify_provider "$binary" platform.linux.defaults 0

    echo "  display and mcu providers, map from the board, nothing else"
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
    platform.linux.mcu \
    platform.linux.touch \
    platform.linux.imu \
    platform.linux.rtc \
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
echo "To see it: switch to a virtual terminal, then"
echo "  ./configure --target $target --target-host --compiler $compiler \\"
echo "      --sdk $sdk --runner native --build debug"
echo "  ./run --target $app_path/"
echo "Three colour bars, rotating twice. Red is 0xf800; bars that come out blue"
echo "mean the provider swapped the RGB565 bytes."
