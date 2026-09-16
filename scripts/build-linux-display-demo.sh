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
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

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
            echo "--run needs DRM master: use a virtual terminal, not a desktop"
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
# difference before comparing. none is left alone: a bare-metal triple means it.
normalize_triple() {
    case "$1" in
        *-unknown-*-*) printf '%s\n' "$1" | sed 's/-unknown-/-/' ;;
        *) printf '%s\n' "$1" ;;
    esac
}

# configure validates the native runner against the compiler the host lane is
# configured with, not against whatever gcc happens to be on PATH. Those differ
# on a machine whose host lane is clang, so ask the same compiler configure will.
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

# Only the machine's own architecture can be built here: the native runner is
# compatible with one triple, and an executable for another machine could not
# be inspected for the properties below with any meaning.
case "$arch" in
    aarch64)
        target=aarch64-linux-gnu
        sdk=linux-aarch64
        board=generic-linux-aarch64
        board_map=platform.linux.generic_aarch64.map
        machine=AArch64
        ;;
    x86_64)
        target=x86_64-linux-gnu
        sdk=linux-x86_64
        board=generic-linux-x86_64
        board_map=platform.linux.generic_x86_64.map
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

# --run is reported rather than gated on a DRM-backed lane: an ordinary desktop
# session refuses DRM master to anything but the compositor and refuses
# /dev/input to anyone outside the input group, so a non-zero exit there
# describes the machine and not the build. A lane that is expected to succeed
# sets run_must_succeed and says why.
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

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  app      $app"

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
    # are expected and are not checked. That check belongs to a bare-metal
    # image, which this is not. Modern drivers default to a
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

# The two interfaces display-demo names, and the four the SDK also binds but
# which nothing in this application reaches. The absences are the assertion.
verify_reached_providers() {
    image=$1
    verify_provider "$image" platform.linux.display 1
    verify_provider "$image" platform.linux.mcu 1
    verify_provider "$image" platform.linux.touch 0
    verify_provider "$image" platform.linux.imu 0
    verify_provider "$image" platform.linux.rtc 0
    verify_provider "$image" platform.linux.stdio 0
}

echo
echo "SDK alone"

# --target-host is not decoration: configure refuses the native runner without
# it, because a runner that executes the image directly only makes sense where
# the target is a hosted platform the build machine can run.
./configure \
    --target "$target" \
    --target-host \
    --compiler "$compiler" \
    --sdk "$sdk" \
    --runner native \
    --build debug

./build --target "$app_path/"

verify_image "$binary"
verify_reached_providers "$binary"

# The map arrives behind the display and MCU providers rather than from the
# application, which names it nowhere.
verify_provider "$binary" platform.linux.defaults 1
if [ -n "$board_map" ]; then
    verify_provider "$binary" "$board_map" 0
fi

echo "  display and mcu providers, map from the SDK, nothing else"

if [ -n "$board" ]; then
    echo
    echo "Board over SDK"

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
    verify_reached_providers "$binary"

    # The board replaces exactly one binding, and the interface it replaces is
    # one the application never mentions.
    verify_provider "$binary" "$board_map" 1
    verify_provider "$binary" platform.linux.defaults 0

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
verify_image "$control_binary"
for provider in \
    platform.linux.display \
    platform.linux.mcu \
    platform.linux.touch \
    platform.linux.imu \
    platform.linux.rtc \
    platform.linux.stdio \
    platform.linux.defaults; do
    verify_provider "$control_binary" "$provider" 0
done
echo "  no provider objects in $control_app"

if [ "$run_app" = yes ]; then
    run_application
fi

./configure
trap - 0

echo
echo "PASS: $test_name"
echo "To see it: switch to a virtual terminal, then"
echo "  ./configure --target $target --target-host --compiler $compiler \\"
echo "      --sdk $sdk --runner native --build debug"
echo "  ./run --target $app_path/"
echo "Three colour bars, rotating twice. Red is 0xf800; bars that come out blue"
echo "mean the provider swapped the RGB565 bytes."
