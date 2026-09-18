#!/bin/sh
# Build the analog smoke application through the Pico SDK bridge and inspect
# the resulting image. This proves provider selection, the ADC and PWM ABI
# symbols, link, and UF2 structure. The analog path needs a wired board run.
set -eu

test_name=build-analog-smoke-pico
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$script_dir"

board=pico
while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            if [ "$#" -lt 2 ]; then
                echo "$test_name: $1 requires a board name" >&2
                exit 64
            fi
            board=$2
            shift 2
            ;;
        -h|--help)
            echo "usage: $0 [-b|--board BOARD]"
            echo "boards: pico, pico-w, pico2-arm, pico2-w-arm, pico2-riscv, pico2-w-riscv"
            exit 0
            ;;
        *)
            echo "$test_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

case "$board" in
    pico|pico-w)
        target=arm-none-eabi
        compiler=arm-none-eabi-gcc
        sdk=pico-arm
        family=rp2040
        ;;
    pico2-arm|pico2-w-arm)
        target=arm-none-eabi
        compiler=arm-none-eabi-gcc
        sdk=pico-arm
        family=rp2350
        ;;
    pico2-riscv|pico2-w-riscv)
        target=riscv32-pico-elf
        compiler=riscv32-pico-elf-gcc
        sdk=pico-riscv
        family=rp2350
        ;;
    *)
        echo "$test_name: unsupported board: $board" >&2
        exit 64
        ;;
esac

nm_command="$target-nm"
readelf_command="$target-readelf"
app=analog-smoke
control_app=target-smoke-any

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

for command_name in "$compiler" "$nm_command" "$readelf_command" cmake; do
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
    --target "$target" \
    --compiler "$compiler" \
    --sdk "$sdk" \
    --board "$board" \
    --build debug

picotool_DIR="$mm_picotool_dir" ./build --target "apps/$app/"

binary="out-target-$target/apps/$app/$app"
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

undefined=$($nm_command -u "$binary")
if [ -n "$undefined" ]; then
    echo "$test_name: unexpected undefined symbols in $binary:" >&2
    echo "$undefined" >&2
    exit 1
fi

case "$target" in
    arm-none-eabi) machine=ARM ;;
    riscv32-pico-elf) machine=RISC-V ;;
esac
if ! "$readelf_command" -h "$binary" | awk -F: -v machine="$machine" '
    $1 ~ /Type/ && $2 ~ /EXEC/ { executable = 1 }
    $1 ~ /Machine/ && index($2, machine) { expected_machine = 1 }
    END { exit !(executable && expected_machine) }
'; then
    echo "$test_name: $binary is not an executable for $machine" >&2
    exit 1
fi

provider_count=$($nm_command -C "$binary" | awk '
    index($0, "initializer for module platform.pico.stdio") { ++count }
    END { print count + 0 }
')
if [ "$provider_count" -ne 1 ]; then
    echo "$test_name: expected one platform.pico.stdio initializer in $binary," >&2
    echo "  got $provider_count" >&2
    exit 1
fi

mcu_count=$($nm_command -C "$binary" | awk '
    index($0, "initializer for module platform.pico.mcu") { ++count }
    END { print count + 0 }
')
if [ "$mcu_count" -ne 1 ]; then
    echo "$test_name: expected one platform.pico.mcu initializer in $binary," >&2
    echo "  got $mcu_count" >&2
    exit 1
fi

for symbol in mm_pico_mcu_adc_configure mm_pico_mcu_adc_read \
              mm_pico_mcu_pwm_configure mm_pico_mcu_pwm_write; do
    if ! "$nm_command" "$binary" | grep -Eq "[[:space:]]${symbol}$"; then
        echo "$test_name: missing analog symbol $symbol in $binary" >&2
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
case "$picotool_info" in
    *"$family"*) ;;
    *)
        echo "$test_name: expected $family identity in $binary.uf2:" >&2
        echo "$picotool_info" >&2
        exit 1
        ;;
esac

# Provider injection follows the application's closure. A portable application
# that does not import mm.stdio must not pay for the USB-console provider.
picotool_DIR="$mm_picotool_dir" ./build --target "apps/$control_app/"
control_binary="out-target-$target/apps/$control_app/$control_app"
control_count=$($nm_command -C "$control_binary" | awk '
    index($0, "initializer for module platform.pico.stdio") { ++count }
    END { print count + 0 }
')
if [ "$control_count" -ne 0 ]; then
    echo "$test_name: unexpected platform.pico.stdio initializer in $control_binary" >&2
    exit 1
fi

./configure
trap - 0

echo "PASS: $test_name"
echo "Hardware check: GP16 through 10 kOhm to GP26, 1 uF from GP26 to ground,"
echo "  GP17 and GP0 unconnected; flash apps/$app/ and open its USB CDC console."
echo "Exit codes: 0 passed/skipped; 1-9 setup or rule failure; 10-13 the"
echo "  analog path read wrong; 14-16 temperature, release, or GPIO return."
echo "Full exit-code table: apps/$app/mm.mdy"
