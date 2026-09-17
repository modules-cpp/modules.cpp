#!/bin/sh
# Build apps/font-demo for the Waveshare RP2350-Touch-LCD-2.8 through the Pico
# SDK bridge, and verify what came out.
#
# The board binds four platform interfaces, but font-demo names only
# mm.display and mm.mcu, so this build proves provider injection follows the
# application's closure and not the board's: exactly one display provider and
# one MCU provider, no touch, IMU or RTC object, and the ST7789 driver behind
# the display. The font tables are plain data that the linker keeps because the
# demo draws with both.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not flash or run it. --flash hands
# the built application to ./flash.sh, which needs the board in BOOTSEL mode.
set -eu

test_name=build-font-demo-rp2350_touch_lcd_28
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

board=rp2350_touch_lcd_28
app=font-demo
app_path=apps/font-demo
flash_app=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        --flash)
            flash_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [--flash]"
            echo "--flash writes the image with picotool; put the board in BOOTSEL first"
            exit 0
            ;;
        *)
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

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
    ./build --target "$app_path/"

binary="out-target-arm-none-eabi/$app_path/$app"
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

# One initializer each. More than one would mean a provider object was linked
# twice; none would mean the interface resolved to nothing and the application
# is talking to an unserved fallback.
verify_provider() {
    provider=$1
    expected=$2
    count=$(arm-none-eabi-nm -C "$binary" | awk -v provider="$provider" '
        index($0, "initializer for module " provider) { ++count }
        END { print count + 0 }
    ')
    if [ "$count" -ne "$expected" ]; then
        echo "$test_name: expected $expected $provider initializer(s) in $binary, got $count" >&2
        exit 1
    fi
}

# The two interfaces the demo reaches, and the three the board binds but the
# demo never mentions. The absences are the assertion.
verify_provider "platform.$board.display" 1
verify_provider platform.pico.mcu 1
verify_provider "platform.$board.touch" 0
verify_provider "platform.$board.imu" 0
verify_provider "platform.$board.rtc" 0

# The controller behind the display provider, which is what catches a board
# wired to the interface but not to its driver.
if ! arm-none-eabi-nm -C "$binary" | grep -q mm::lcd::st7789; then
    echo "$test_name: no mm::lcd::st7789 symbols in $binary" >&2
    exit 1
fi

# Both font tables: the demo draws two lines in each, so neither may be
# garbage-collected out of the image.
for table in kMono12 kMono16; do
    if ! arm-none-eabi-nm -C "$binary" | grep -q "mm::fonts::${table}Data"; then
        echo "$test_name: no mm::fonts::$table table in $binary" >&2
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

echo "  display and mcu providers, st7789 driver, both font tables, nothing else"

# Flashing happens while the lane is still configured for the board: ./flash.sh
# reads out/config.mdy to find the image, and restore_host would point it back
# at the host lane.
if [ "$flash_app" = yes ]; then
    echo
    echo "Flash"
    picotool_DIR="$mm_picotool_dir" ./flash.sh "$app_path/"
fi

./configure
trap - 0

echo "PASS: $test_name"
echo "To see it: hold BOOTSEL, plug the board in, then"
echo "  scripts/build-font-demo-rp2350_touch_lcd_28.sh --flash"
echo "Four centred lines, black on white, two at 16px and two at 12px, the last"
echo "one Polish, then every glyph of the 12px table in four lines of 34,"
echo "held four seconds."
