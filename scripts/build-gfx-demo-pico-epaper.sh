#!/bin/sh
# Build apps/gfx-demo for a Raspberry Pi Pico with a Waveshare Pico-ePaper-2.66
# through the Pico SDK bridge, and verify what came out.
#
# Two boards carry that panel: pico_epaper is the black and white one, and
# pico_epaper_b the black, white and red one. Both bind mm.display to a
# board-owned provider over the SSD1680 driver, and mm.mcu to the Pico SDK's.
# gfx-demo names exactly those two interfaces, so the image must carry one
# object for each and the driver between them, and nothing for the interfaces
# the demo never mentions.
#
# The panel is one bit deep, so this is the lane where the demo's packed-row
# path runs on real hardware.
#
# The prebuilt Pico tools default to platforms/pico/pico-sdk. Set MM_PICO_TOOLS
# or picotool_DIR to select another installation.
#
# This builds and inspects firmware; it does not flash or run it. --flash hands
# the built application to ./flash.sh, which needs the board in BOOTSEL mode.
set -eu

test_name=build-gfx-demo-pico-epaper
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

app=gfx-demo
app_path=apps/gfx-demo
panel=bw
flash_app=no

while [ "$#" -gt 0 ]; do
    case "$1" in
        --panel)
            if [ "$#" -lt 2 ]; then
                echo "$test_name: --panel requires bw, bwr, or b" >&2
                exit 64
            fi
            panel=$2
            shift 2
            ;;
        --flash)
            flash_app=yes
            shift
            ;;
        -h|--help)
            echo "usage: $0 [--panel bw|bwr|b] [--flash]"
            echo "--panel bw is the Pico-ePaper-2.66 (default); bwr or b the -B"
            echo "--flash writes the image with picotool; put the board in BOOTSEL first"
            exit 0
            ;;
        *)
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

case "$panel" in
    bw)
        board=pico_epaper
        ;;
    bwr|b)
        board=pico_epaper_b
        ;;
    *)
        echo "$test_name: unknown panel: $panel (expected bw, bwr, or b)" >&2
        exit 64
        ;;
esac

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
echo "  panel  $panel"
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

# The two interfaces the demo reaches, and the other panel's provider, which
# must not be here: two boards share one driver, and only the selected one
# may register.
verify_provider "platform.$board.display" 1
verify_provider platform.pico.mcu 1
case "$board" in
    pico_epaper)   verify_provider platform.pico_epaper_b.display 0 ;;
    pico_epaper_b) verify_provider platform.pico_epaper.display 0 ;;
esac

# The controller behind the display provider must reach the image.
for symbol in mm::epaper::ssd1680; do
    if ! arm-none-eabi-nm -C "$binary" | grep -q "$symbol"; then
        echo "$test_name: no $symbol symbols in $binary" >&2
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
# Both e-paper boards sit on the original Pico.
case "$picotool_info" in
    *rp2040*) ;;
    *)
        echo "$test_name: expected rp2040 identity in $binary.uf2:" >&2
        echo "$picotool_info" >&2
        exit 1
        ;;
esac

echo "  display and mcu providers, ssd1680 driver, nothing else"

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
echo "To see it: hold BOOTSEL, plug the Pico in, then"
echo "  scripts/build-gfx-demo-pico-epaper.sh --panel $panel --flash"
echo "Four full refreshes, four seconds apart: a black framed X, filled"
echo "centre box, and corner dots on white, turned a quarter clockwise each"
echo "time, before the panel sleeps."
