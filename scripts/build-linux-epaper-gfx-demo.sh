#!/bin/sh
# Build apps/gfx-demo for a native Linux lane on the emulated e-paper board,
# and verify what came out.
#
# The e-paper board rebinds two interfaces: mm.display to the real SSD1680
# controller, and mm.mcu to the emulated chip behind the seam the controller
# talks through. gfx-demo names both, so the image must carry one object for
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

test_name=build-linux-epaper-gfx-demo
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

app=gfx-demo
app_path=apps/gfx-demo
run_app=no
compiler=
run_must_succeed=yes
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
        -c|--compiler)
            if [ "$#" -lt 2 ]; then
                echo "$test_name: $1 requires a compiler" >&2
                exit 64
            fi
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
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

# clang normalises a target triple to four fields and writes unknown where there
# is no vendor; Debian's GCC omits the field. aarch64-linux-gnu and
# aarch64-unknown-linux-gnu are the same machine, so collapse that one
# difference before comparing.
normalize_triple() {
    case "$1" in
        *-unknown-*-*) printf '%s\n' "$1" | sed 's/-unknown-/-/' ;;
        *) printf '%s\n' "$1" ;;
    esac
}

# configure validates the native runner against the compiler the host lane is
# configured with, not against whatever gcc happens to be on PATH.
configured_host_compiler() {
    if [ -f out/config.mdy ]; then
        sed -n 's/^host-compiler: *//p' out/config.mdy | head -1
    fi
}

host_cxx=$(configured_host_compiler)
: "${host_cxx:=g++}"

if [ -z "$arch" ]; then
    case $(normalize_triple "$(${host_cxx:-g++} -dumpmachine 2>/dev/null)") in
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
        board=epaper-linux-aarch64
        inherited_map=platform.linux.generic_aarch64.map
        machine=AArch64
        ;;
    x86_64)
        target=x86_64-linux-gnu
        sdk=linux-x86_64
        board=epaper-linux-x86_64
        inherited_map=platform.linux.generic_x86_64.map
        machine=X86-64
        ;;
    *)
        echo "$test_name: unsupported architecture: $arch" >&2
        exit 64
        ;;
esac

: "${compiler:=$target-g++}"
nm_command="$target-nm"
readelf_command="$target-readelf"

for command_name in "$compiler" "$nm_command" "$readelf_command"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "$test_name: required command not found: $command_name" >&2
        echo "  install the $target toolchain, or pass --arch" >&2
        exit 65
    fi
done

host_triple=$(normalize_triple "$("$host_cxx" -dumpmachine 2>/dev/null)")
if [ "$host_triple" != "$(normalize_triple "$target")" ]; then
    echo "$test_name: $target is not this machine's triple" >&2
    echo "  $host_cxx reports $host_triple, and the native runner accepts only that" >&2
    exit 65
fi

# The emulated device declares the SDL2 system package for its window, so an
# absent package is not an absent checkout. Nothing would catch it before the
# compiler did.
if [ ! -f /usr/include/SDL2/SDL.h ]; then
    echo "$test_name: SDL2 development headers not found" >&2
    echo "  install libsdl2-dev" >&2
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

# The exit codes are the steps of apps/gfx-demo/main.cpp, in order.
explain_run() {
    case "$1" in
        0) echo "  the glow, tunnel, and porthole were drawn in four orientations" ;;
        1) echo "  1 is display.initialize: the controller's reset or busy" ;
           echo "  handshake with the emulated chip failed" ;;
        2) echo "  2 is the geometry check: the panel is neither one nor" ;
           echo "  sixteen bits deep, or reported no size" ;;
        3) echo "  3 is the frame budget: a panel side exceeds 480 pixels" ;;
        4) echo "  4 is display.clear" ;;
        5) echo "  5 is gfx drawing or rotation" ;;
        6) echo "  6 is gfx.write: the controller refused the packed frame" ;;
        7) echo "  7 is display.refresh: the emulated refresh did not complete," ;
           echo "  or the window could not be shown" ;;
        8) echo "  8 is mcu.delay_ms: the hold did not complete" ;;
        9) echo "  9 is display.sleep" ;;
        *) echo "  see apps/gfx-demo/main.cpp for that step" ;;
    esac
}

run_application() {
    echo
    echo "Run"
    set +e
    "./$binary"
    run_status=$?
    set -e
    echo "  $app exited $run_status"
    explain_run "$run_status"
    if [ "$run_must_succeed" = yes ] && [ "$run_status" -ne 0 ]; then
        echo "$test_name: $app was expected to succeed on this lane" >&2
        exit 1
    fi
}

echo "Native Linux lane, emulated e-paper"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  board    $board"
echo "  app      $app"
echo "  sdl2     $(pkg-config --modversion sdl2 2>/dev/null || echo 'headers present')"

# One initializer each. More than one would mean a provider object was linked
# twice; none would mean the interface resolved to nothing and the application
# is talking to an unserved fallback.
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
    # A hosted executable is linked against the C library, so undefined symbols
    # are expected and are not checked. Modern drivers default to a
    # position-independent executable, so DYN is as correct as EXEC here.
    if ! "$readelf_command" -h "$image" | awk -F: -v machine="$machine" '
        $1 ~ /Type/ && ($2 ~ /EXEC/ || $2 ~ /DYN/) { executable = 1 }
        $1 ~ /Machine/ && index($2, machine) { expected_machine = 1 }
        END { exit !(executable && expected_machine) }
    '; then
        echo "$test_name: $image is not an executable for $machine" >&2
        exit 1
    fi
}

verify_library() {
    image=$1
    count=$("$readelf_command" -d "$image" | grep -c 'Shared library: \[libSDL2' || true)
    if [ "$count" -eq 0 ]; then
        echo "$test_name: $image does not link SDL2" >&2
        echo "  the emulated device's window did not reach the link line" >&2
        exit 1
    fi
}

# The controller behind the emulated display provider must reach the image.
verify_symbols() {
    image=$1
    for symbol in mm::epaper::ssd1680; do
        if ! $nm_command -C "$image" | grep -q "$symbol"; then
            echo "$test_name: no $symbol symbols in $image" >&2
            exit 1
        fi
    done
}

echo
echo "E-paper board"

./configure \
    --target "$target" \
    --target-host \
    --compiler "$compiler" \
    --sdk "$sdk" \
    --board "$board" \
    --runner native \
    --build debug

./build --target "$app_path/"

verify_image "$binary"
verify_library "$binary"
verify_symbols "$binary"
if "$nm_command" -C "$binary" | grep -q 'mm::fonts'; then
    echo "$test_name: unexpected mm::fonts symbols in $binary" >&2
    exit 1
fi

# The two interfaces gfx-demo reaches, both rebound by the board, the chip
# that arrives behind them, and the six providers the SDK binds that nothing
# here mentions. The named device map is absent too: the DRM and evdev
# providers are what use it, and the emulated device needs no device names.
verify_provider "$binary" platform.linux.epaper.display 1
verify_provider "$binary" platform.linux.epaper.device 1
verify_provider "$binary" platform.linux.epaper.chip 1
verify_provider "$binary" "$inherited_map" 0
verify_provider "$binary" platform.linux.display 0
verify_provider "$binary" platform.linux.mcu 0
verify_provider "$binary" platform.linux.sdl 0
verify_provider "$binary" platform.linux.touch 0
verify_provider "$binary" platform.linux.imu 0
verify_provider "$binary" platform.linux.rtc 0
verify_provider "$binary" platform.linux.stdio 0
verify_provider "$binary" platform.linux.defaults 0

echo "  emulated chip and controller, SDL2 on the link line,"
echo "  nothing else"

if [ "$run_app" = yes ]; then
    run_application
fi

./configure
trap - 0

echo
echo "PASS: $test_name"
echo "To watch it: scripts/build-linux-epaper-gfx-demo.sh --run"
echo "A 152 by 296 window opens when the first refresh completes: a dithered"
echo "glow around a porthole full of rays, nested frames, and an off-centre"
echo "tick, black on white. The scene turns a quarter clockwise three more"
echo "times, four seconds apart."
