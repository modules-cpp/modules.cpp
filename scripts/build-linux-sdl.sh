#!/bin/sh
# Build the SDL2 display and touch backend for a native Linux lane, and verify
# what came out.
#
# This is the one script that proves two things nothing else can.
#
# A library reaches the link line. link-input on a kind: library was declared,
# validated and carried for two releases without any command consuming it, so
# until now a manifest could name a library and the link would fail on undefined
# symbols. A NEEDED entry for libSDL2 is that feature working.
#
# One provider module serves two interfaces. SDL has one video subsystem, one
# window and one event queue, so platform.linux.sdl implements mm.display and
# mm.touch together and the board binds it twice. An application reaching both
# must still link exactly one of its objects, not two.
#
# And the negative half, which matters more than either: a lane that does not
# name this board links no SDL2 at all. The SDK still binds DRM and evdev, so
# an installation without libsdl2-dev is only a problem for somebody who asked
# for the window.
#
# Unlike the other Linux scripts, --run here is expected to work on an ordinary
# desktop. That is the entire reason this backend exists: DRM needs DRM master
# and a compositor holds it, so anything visual there means a virtual terminal.
# A window does not.
set -eu

test_name=build-linux-sdl
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/linux.sh"
mm_enter_root

app=display-demo
app_path=apps/display-demo
both_app=board-smoke
both_path=apps/board-smoke
control_app=target-smoke-any
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
both_binary="out-target-$target/$both_path/$both_app"
control_binary="out-target-$target/apps/$control_app/$control_app"

explain_run() {
    case "$1" in
        0) echo "  three colour bars were drawn and held, in a window" ;;
        *) echo "  a window was expected: a headless session or an" ;
           echo "  SDL_VIDEODRIVER override would explain this" ;;
    esac
}

echo "Native Linux lane, SDL2 backend"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    ${board:-none}"
echo "  sdl2     $(pkg-config --modversion sdl2 2>/dev/null || echo 'headers present')"

# A NEEDED entry is the link-input feature working, and its absence elsewhere is
# the closure rule working. Both directions are asserted, because either alone
# would pass for the wrong reason.
echo
echo "SDK alone: no board, so no SDL2"

mm_linux_configure
./build --target "$app_path/"

mm_linux_verify_image "$binary"
mm_verify_provider "$binary" platform.linux.display 1
mm_verify_provider "$binary" platform.linux.sdl 0
mm_linux_verify_sdl2 "$binary" no

echo "  DRM display provider, and the image links no SDL2"

if [ -z "$board" ]; then
    echo
    echo "SDL board: skipped, none is defined for $arch"
    mm_leave_host
    echo
    echo "PASS: $test_name"
    exit 0
fi

echo
echo "SDL board: the display and touch providers move into a window"

mm_linux_configure "$board"
./build --target "$app_path/"

mm_linux_verify_image "$binary"
mm_verify_provider "$binary" platform.linux.sdl 1
mm_verify_provider "$binary" platform.linux.display 0
mm_verify_provider "$binary" platform.linux.touch 0
# The board replaced two interfaces and inherited the rest.
mm_verify_provider "$binary" platform.linux.mcu 1
mm_verify_provider "$binary" "$inherited_map" 1
mm_linux_verify_sdl2 "$binary" yes "link-input did not reach the link line"

echo "  SDL provider, no DRM or evdev, and SDL2 on the link line"

echo
echo "One module, two interfaces"

# board-smoke reaches mm.display and mm.touch, which this board binds to the
# same module. One object, not two: a provider merged twice would register its
# objects twice and race with itself.
./build --target "$both_path/"

mm_linux_verify_image "$both_binary"
mm_verify_provider "$both_binary" platform.linux.sdl 1
mm_verify_provider "$both_binary" platform.linux.display 0
mm_verify_provider "$both_binary" platform.linux.touch 0
mm_verify_provider "$both_binary" platform.linux.imu 1
mm_verify_provider "$both_binary" platform.linux.rtc 1
mm_linux_verify_sdl2 "$both_binary" yes "link-input did not reach the link line"

echo "  two interfaces resolved to one provider object"

echo
echo "Control"

./build --target "apps/$control_app/"
mm_linux_verify_image "$control_binary"
mm_verify_provider "$control_binary" platform.linux.sdl 0
mm_linux_verify_sdl2 "$control_binary" no

echo "  no provider objects and no SDL2 in $control_app"

if [ "$run_app" = yes ]; then
    mm_linux_run
fi

mm_leave_host

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-linux-sdl.sh --run"
echo "Three colour bars, rotating twice, in a window. Red is 0xf800; bars that"
echo "come out blue mean the RGB565 byte swap was dropped."
