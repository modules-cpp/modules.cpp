#!/bin/sh
# Build apps/board-smoke for the Waveshare RP2350-Touch-LCD-2.8 through the
# Pico SDK bridge, and verify what came out.
#
# The board binds four platform interfaces at once, so this is the one build
# that proves provider injection resolves more than one: mm.display, mm.touch,
# mm.imu, and mm.rtc must each contribute exactly one registration to the image,
# alongside the MCU provider.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not run it. A successful build is
# not a claim that the panel lit up, and the pin map this board carries has not
# been confirmed against Waveshare's schematic.
set -eu

test_name=build-board-smoke-rp2350_touch_lcd_28
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

board=rp2350_touch_lcd_28
app=board-smoke

mm_pico_tools=${MM_PICO_TOOLS:-"$script_dir/platforms/pico/pico-sdk"}
if [ ! -d "$mm_pico_tools" ]; then
    echo "$test_name: Pico tools directory not found: $mm_pico_tools" >&2
    echo "  run platforms/pico/install-sdk-tools.sh to install them" >&2
    exit 65
fi
mm_pico_tools=$(CDPATH= cd -- "$mm_pico_tools" && pwd)

mm_picotool_dir=${picotool_DIR:-"$mm_pico_tools/picotool"}
if [ ! -d "$mm_picotool_dir" ]; then
    echo "$test_name: picotool package directory not found: $mm_picotool_dir" >&2
    exit 65
fi
mm_picotool_dir=$(CDPATH= cd -- "$mm_picotool_dir" && pwd)
mm_picotool="$mm_picotool_dir/picotool"

if [ ! -x "$mm_picotool" ]; then
    echo "$test_name: picotool executable not found: $mm_picotool" >&2
    exit 65
fi
if [ ! -f "$mm_picotool_dir/picotoolConfig.cmake" ] && \
   [ ! -f "$mm_picotool_dir/picotool-config.cmake" ]; then
    echo "$test_name: picotool CMake package not found in $mm_picotool_dir" >&2
    exit 65
fi
if [ ! -f platforms/pico/sdk/pico-sdk/upstream/README.md ]; then
    echo "$test_name: Pico SDK checkout is absent; run platforms/pico/sdk/pico-sdk/vendor.sh" >&2
    exit 65
fi

PATH="$mm_pico_tools/bin:$PATH"
export PATH

for command_name in \
    arm-none-eabi-gcc \
    arm-none-eabi-g++ \
    arm-none-eabi-nm \
    arm-none-eabi-readelf \
    cmake; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "$test_name: required command not found: $command_name" >&2
        exit 65
    fi
done

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

echo "Pico SDK tools"
echo "  bundle $mm_pico_tools"
echo "  $($mm_picotool version)"
echo "  board  $board"
echo "  app    $app"

./configure \
    --target arm-none-eabi \
    --compiler arm-none-eabi-gcc \
    --sdk pico-arm \
    --board "$board" \
    --build debug

picotool_DIR="$mm_picotool_dir" \
    ./build --target "apps/$app/"

binary="out-target-arm-none-eabi/apps/$app/$app"
for artifact in \
    "$binary" \
    "$binary.bin" \
    "$binary.hex" \
    "$binary.elf.map" \
    "$binary.uf2"; do
    if [ ! -f "$artifact" ]; then
        echo "$test_name: missing artifact: $artifact" >&2
        exit 1
    fi
done

undefined=$(arm-none-eabi-nm -u "$binary")
if [ -n "$undefined" ]; then
    echo "$test_name: unexpected undefined symbols in $binary:" >&2
    echo "$undefined" >&2
    exit 1
fi

if ! arm-none-eabi-readelf -h "$binary" | awk -F: '
    $1 ~ /Type/ && $2 ~ /EXEC/ { executable = 1 }
    $1 ~ /Machine/ && $2 ~ /ARM/ { arm = 1 }
    END { exit !(executable && arm) }
'; then
    echo "$test_name: $binary is not an ARM executable" >&2
    exit 1
fi

verify_provider() {
    provider=$1
    count=$(arm-none-eabi-nm -C "$binary" | awk -v provider="$provider" '
        index($0, "initializer for module " provider) { ++count }
        END { print count + 0 }
    ')
    if [ "$count" -ne 1 ]; then
        echo "$test_name: expected one $provider initializer in $binary, got $count" >&2
        exit 1
    fi
}

# One registration each. More than one would mean a provider object was linked
# twice; none would mean the interface resolved to nothing and the application
# is talking to an unserved fallback.
verify_provider "platform.$board.display"
verify_provider "platform.$board.touch"
verify_provider "platform.$board.imu"
verify_provider "platform.$board.rtc"
verify_provider platform.pico.mcu

# The drivers the providers bind, rather than the providers themselves: this is
# what catches a board wired to an interface but not to a controller.
for driver in \
    mm::lcd::st7789 \
    mm::touch::cst328 \
    mm::imu::qmi8658 \
    mm::rtc::pcf85063; do
    if ! arm-none-eabi-nm -C "$binary" | grep -q "$driver"; then
        echo "$test_name: no $driver symbols in $binary" >&2
        exit 1
    fi
done

cmake \
    "-DMM_UF2=$binary.uf2" \
    -P platforms/pico/sdk/pico-sdk/cmake/validate-uf2.cmake

if ! picotool_info=$($mm_picotool info "$binary.uf2" 2>&1); then
    echo "$test_name: picotool rejected $binary.uf2:" >&2
    echo "$picotool_info" >&2
    exit 1
fi
# This board is an RP2350A in its secure ARM profile, which is what deriving
# from pico2-arm selects.
case "$picotool_info" in
    *rp2350*) ;;
    *)
        echo "$test_name: expected rp2350 identity in $binary.uf2:" >&2
        echo "$picotool_info" >&2
        exit 1
        ;;
esac

./configure
trap - 0

echo "PASS: $test_name"
