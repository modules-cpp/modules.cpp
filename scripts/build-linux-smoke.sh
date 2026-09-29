#!/bin/sh
# Build platforms/linux/apps/linux-smoke for a native Linux lane, and verify
# what came out.
#
# The Linux SDK binds seven interfaces at once, so like the board-smoke script
# this proves provider injection resolves more than one. What it proves that no
# other script does is the tier: platform.linux.map is itself a platform
# interface, the SDK binds platform.linux.defaults to it, and a board overrides
# that one binding without disturbing the other six. So this builds twice, once
# with the SDK alone and once with the board, and asserts the map provider
# swaps and nothing else does.
#
# The lane is the target lane aimed at the build machine's own triple. That is
# not a contradiction: a target lane means "not the host compiler", and
# aarch64-linux-gnu-g++ is a different driver from g++ even on an aarch64
# machine.
#
# This builds and inspects an executable; it does not judge one by running it.
# linux-smoke's exit code depends on which kernel devices this machine has and
# whether a compositor holds the display, so a run is reported and never gates.
# Use --run to see it.
set -eu

test_name=build-linux-smoke
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
mm_enter_root

app=linux-smoke
app_path=platforms/linux/apps/linux-smoke
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
            echo "--run reports each facility; a desktop session refuses display and touch"
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
        0) echo "  every facility this machine has initialised and answered" ;;
        6) echo "  6 is display.initialize: a compositor holds DRM master" ;;
        8) echo "  8 is touch.initialize: /dev/input needs the input group" ;;
        *) echo "  see platforms/linux/apps/linux-smoke/main.cpp for that step" ;;
    esac
}

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  app      $app"

# The six device interfaces the SDK binds. These are the same whichever map
# provider is selected, which is the half of the assertion that says a board
# overriding one binding disturbs nothing else.
verify_device_providers() {
    image=$1
    mm_verify_provider "$image" platform.linux.stdio 1
    mm_verify_provider "$image" platform.linux.mcu 1
    mm_verify_provider "$image" platform.linux.rtc 1
    mm_verify_provider "$image" platform.linux.display 1
    mm_verify_provider "$image" platform.linux.touch 1
    mm_verify_provider "$image" platform.linux.imu 1
}

echo
echo "SDK alone"

mm_linux_configure

./build --target "$app_path/"

mm_linux_verify_image "$binary"
verify_device_providers "$binary"

# The SDK's own map provider, and not a board's.
mm_verify_provider "$binary" platform.linux.defaults 1
if [ -n "$board_map" ]; then
    mm_verify_provider "$binary" "$board_map" 0
fi

echo "  seven providers, map from the SDK"

if [ -n "$board" ]; then
    echo
    echo "Board over SDK"

    mm_linux_configure "$board"

    ./build --target "$app_path/"

    mm_linux_verify_image "$binary"
    verify_device_providers "$binary"

    # The board replaces exactly one binding. A generic map still present would
    # mean both providers were linked and their registrations raced; the
    # board's map absent would mean the override did not take.
    mm_verify_provider "$binary" "$board_map" 1
    mm_verify_provider "$binary" platform.linux.defaults 0

    echo "  seven providers, map from the board"
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
    platform.linux.stdio \
    platform.linux.mcu \
    platform.linux.rtc \
    platform.linux.display \
    platform.linux.touch \
    platform.linux.imu \
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
echo "Full qualification: run from a virtual terminal, with membership of the"
echo "video and input groups, where DRM master is available."
