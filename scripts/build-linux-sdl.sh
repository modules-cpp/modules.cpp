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
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

app=display-demo
app_path=apps/display-demo
both_app=board-smoke
both_path=apps/board-smoke
control_app=target-smoke-any
run_app=no
arch=

while [ "$#" -gt 0 ]; do
    case "$1" in
        -a|--arch)
            if [ "$#" -lt 2 ]; then
                echo "$test_name: $1 requires an architecture" >&2
                exit 64
            fi
            arch=$2
            shift 2
            ;;
        --run)
            run_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [-a|--arch aarch64|x86_64] [--run]"
            echo "architectures: aarch64, x86_64 (default: this machine's)"
            echo "--run opens a window; no virtual terminal is needed"
            exit 0
            ;;
        *)
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

if [ -z "$arch" ]; then
    case $(gcc -dumpmachine 2>/dev/null) in
        aarch64-*) arch=aarch64 ;;
        x86_64-*)  arch=x86_64 ;;
        *)
            echo "$test_name: cannot infer architecture; pass --arch" >&2
            exit 64
            ;;
    esac
fi

case "$arch" in
    aarch64)
        target=aarch64-linux-gnu
        sdk=linux-aarch64
        board=sdl-linux-aarch64
        machine=AArch64
        ;;
    x86_64)
        target=x86_64-linux-gnu
        sdk=linux-x86_64
        board=
        machine=X86-64
        ;;
    *)
        echo "$test_name: unsupported architecture: $arch" >&2
        exit 64
        ;;
esac

compiler="$target-g++"
nm_command="$target-nm"
readelf_command="$target-readelf"

for command_name in "$compiler" "$nm_command" "$readelf_command"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "$test_name: required command not found: $command_name" >&2
        echo "  install the $target toolchain, or pass --arch" >&2
        exit 65
    fi
done

if [ "$(gcc -dumpmachine 2>/dev/null)" != "$target" ]; then
    echo "$test_name: $target is not this machine's triple" >&2
    echo "  the native runner accepts only $(gcc -dumpmachine)" >&2
    exit 65
fi

# The library declares a system package, so its source is a record of what was
# observed rather than a vendored tree, and an absent package is not an absent
# checkout. Nothing would catch it before the compiler did, so it is caught
# here, by name.
if [ ! -f /usr/include/SDL2/SDL.h ]; then
    echo "$test_name: SDL2 development headers not found" >&2
    echo "  install libsdl2-dev, or select a lane that does not bind this board" >&2
    exit 65
fi

if [ ! -x out/bin/configure ] || [ ! -x out/bin/build ]; then
    echo "$test_name: host tools not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

restore_host() {
    status=$?
    trap - 0
    if ! ./configure >/dev/null 2>&1; then
        echo "$test_name: failed to restore the host configuration" >&2
        [ "$status" -ne 0 ] || status=1
    fi
    exit "$status"
}
trap restore_host 0

binary="out-target-$target/$app_path/$app"
both_binary="out-target-$target/$both_path/$both_app"
control_binary="out-target-$target/apps/$control_app/$control_app"

echo "Native Linux lane, SDL2 backend"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    ${board:-none}"
echo "  sdl2     $(pkg-config --modversion sdl2 2>/dev/null || echo 'headers present')"

verify_provider() {
    image=$1
    provider=$2
    expected=$3
    count=$($nm_command -C "$image" | awk -v provider="$provider" '
        index($0, "initializer for module " provider) { ++count }
        END { print count + 0 }
    ')
    if [ "$count" -ne "$expected" ]; then
        echo "$test_name: expected $expected $provider initializer(s) in" >&2
        echo "  $image, got $count" >&2
        exit 1
    fi
}

verify_image() {
    image=$1
    if [ ! -f "$image" ]; then
        echo "$test_name: missing artifact: $image" >&2
        exit 1
    fi
    if ! "$readelf_command" -h "$image" | awk -F: -v machine="$machine" '
        $1 ~ /Type/ && ($2 ~ /EXEC/ || $2 ~ /DYN/) { executable = 1 }
        $1 ~ /Machine/ && index($2, machine) { expected_machine = 1 }
        END { exit !(executable && expected_machine) }
    '; then
        echo "$test_name: $image is not an executable for $machine" >&2
        exit 1
    fi
}

# A NEEDED entry is the link-input feature working, and its absence elsewhere is
# the closure rule working. Both directions are asserted, because either alone
# would pass for the wrong reason.
verify_library() {
    image=$1
    expected=$2
    count=$("$readelf_command" -d "$image" | grep -c 'Shared library: \[libSDL2' || true)
    if [ "$expected" = yes ] && [ "$count" -eq 0 ]; then
        echo "$test_name: $image does not link SDL2" >&2
        echo "  link-input did not reach the link line" >&2
        exit 1
    fi
    if [ "$expected" = no ] && [ "$count" -ne 0 ]; then
        echo "$test_name: $image links SDL2 and should not" >&2
        exit 1
    fi
}

configure_lane() {
    if [ -n "$1" ]; then
        ./configure \
            --target "$target" \
            --target-host \
            --compiler "$compiler" \
            --sdk "$sdk" \
            --board "$1" \
            --runner native \
            --build debug
    else
        ./configure \
            --target "$target" \
            --target-host \
            --compiler "$compiler" \
            --sdk "$sdk" \
            --runner native \
            --build debug
    fi
}

echo
echo "SDK alone: no board, so no SDL2"

configure_lane ""
./build --target "$app_path/"

verify_image "$binary"
verify_provider "$binary" platform.linux.display 1
verify_provider "$binary" platform.linux.sdl 0
verify_library "$binary" no

echo "  DRM display provider, and the image links no SDL2"

if [ -z "$board" ]; then
    echo
    echo "SDL board: skipped, none is defined for $arch"
    ./configure
    trap - 0
    echo
    echo "PASS: $test_name"
    exit 0
fi

echo
echo "SDL board: the display and touch providers move into a window"

configure_lane "$board"
./build --target "$app_path/"

verify_image "$binary"
verify_provider "$binary" platform.linux.sdl 1
verify_provider "$binary" platform.linux.display 0
verify_provider "$binary" platform.linux.touch 0
# The board replaced two interfaces and inherited the rest.
verify_provider "$binary" platform.linux.mcu 1
verify_provider "$binary" "platform.linux.generic_aarch64.map" 1
verify_library "$binary" yes

echo "  SDL provider, no DRM or evdev, and SDL2 on the link line"

echo
echo "One module, two interfaces"

# board-smoke reaches mm.display and mm.touch, which this board binds to the
# same module. One object, not two: a provider merged twice would register its
# objects twice and race with itself.
./build --target "$both_path/"

verify_image "$both_binary"
verify_provider "$both_binary" platform.linux.sdl 1
verify_provider "$both_binary" platform.linux.display 0
verify_provider "$both_binary" platform.linux.touch 0
verify_provider "$both_binary" platform.linux.imu 1
verify_provider "$both_binary" platform.linux.rtc 1
verify_library "$both_binary" yes

echo "  two interfaces resolved to one provider object"

echo
echo "Control"

./build --target "apps/$control_app/"
verify_image "$control_binary"
verify_provider "$control_binary" platform.linux.sdl 0
verify_library "$control_binary" no

echo "  no provider objects and no SDL2 in $control_app"

if [ "$run_app" = yes ]; then
    echo
    echo "Run"
    # Unlike the DRM lane this is expected to succeed in an ordinary session.
    # A window opens for about five seconds.
    set +e
    "./$binary"
    run_status=$?
    set -e
    echo "  $app exited $run_status"
    if [ "$run_status" -ne 0 ]; then
        echo "  a window was expected here; SDL_VIDEODRIVER or a headless" >&2
        echo "  session would explain a failure" >&2
        exit 1
    fi
fi

./configure
trap - 0

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-linux-sdl.sh --run"
echo "Three colour bars, rotating twice, in a window. Red is 0xf800; bars that"
echo "come out blue mean the RGB565 byte swap was dropped."
