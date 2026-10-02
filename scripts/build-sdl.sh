#!/bin/sh
# The SDL2 board's own proof, in three builds on this machine's architecture.
#
# A library reaches the link line: display-demo on the SDK alone uses the DRM
# provider and must not link SDL2, and on the SDL board it uses the SDL
# provider and must. One provider module serves two interfaces: board-smoke
# reaches mm.display and mm.touch, which the SDL board binds to the one
# platform.linux.sdl module, and the image must carry it once, not twice. Every
# provider count is read from the manifests by scripts/build-linux.sh.
#
# --run runs the SDL build of display-demo, which opens a window and is
# expected to work on an ordinary desktop. -a, -c, --keep, and --dry-run pass
# through to every build.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-sdl
. "$(dirname -- "$0")/lib/common.sh"
mm_enter_root

passed=
run=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run) run=--run-must-succeed ;;
        -h|--help)
            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) passed="$passed $1" ;;
    esac
    shift
done

# shellcheck disable=SC2086
{
    echo "SDK alone: the DRM provider, and no SDL2"
    sh scripts/build-linux.sh --board sdk --app display-demo --no-library SDL2 $passed
    echo
    echo "SDL board: the display and touch providers in a window"
    sh scripts/build-linux.sh --board sdl --app display-demo $run $passed
    echo
    echo "One module, two interfaces"
    sh scripts/build-linux.sh --board sdl --app board-smoke --control target-smoke-any $passed
}

case " $passed " in *" --dry-run "*) exit 0 ;; esac

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-sdl.sh --run"
echo "Three colour bars, rotating twice, in a window. Red is 0xf800; bars that"
echo "come out blue mean the RGB565 byte swap was dropped."
