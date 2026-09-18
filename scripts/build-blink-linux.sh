#!/bin/sh
# Build apps/blink for a native Linux lane, and verify what came out.
#
# Like the other Linux scripts, this proves provider injection resolves the
# application's closure: apps/blink uses mm.sketch, which uses mm.mcu and
# mm.stdio. On Linux, this resolves to platform.linux.mcu, platform.linux.stdio,
# and platform.linux.defaults (or the board map). The four unused providers
# (display, touch, imu, rtc) must be absent from the image.
set -eu

test_name=build-blink-linux
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

app=blink
app_path=apps/blink
control_app=target-smoke-any
run_app=no
compiler=
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
            exit 0
            ;;
        *)
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

normalize_triple() {
    case "$1" in
        *-unknown-*-*) printf '%s\n' "$1" | sed 's/-unknown-/-/' ;;
        *) printf '%s\n' "$1" ;;
    esac
}

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

echo "Native Linux lane"
echo "  target   $target"
echo "  compiler $compiler"
echo "  sdk      $sdk"
echo "  app      $app"

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

verify_reached_providers() {
    image=$1
    verify_provider "$image" platform.linux.mcu 1
    verify_provider "$image" platform.linux.stdio 1
    verify_provider "$image" platform.linux.display 0
    verify_provider "$image" platform.linux.touch 0
    verify_provider "$image" platform.linux.imu 0
    verify_provider "$image" platform.linux.rtc 0
}

echo
echo "SDK alone"

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
verify_provider "$binary" platform.linux.defaults 1
if [ -n "$board_map" ]; then
    verify_provider "$binary" "$board_map" 0
fi

echo "  mcu and stdio providers, map from SDK, no display/touch/imu/rtc"

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
    verify_provider "$binary" "$board_map" 1
    verify_provider "$binary" platform.linux.defaults 0

    echo "  mcu and stdio providers, map from the board, no display/touch/imu/rtc"
fi

echo
echo "Control"

./build --target "apps/$control_app/"
verify_image "$control_binary"
for provider in \
    platform.linux.display \
    platform.linux.touch \
    platform.linux.imu \
    platform.linux.rtc \
    platform.linux.mcu \
    platform.linux.stdio \
    platform.linux.defaults; do
    verify_provider "$control_binary" "$provider" 0
done
echo "  no provider objects in $control_app"

./configure
trap - 0

echo
echo "PASS: $test_name"
