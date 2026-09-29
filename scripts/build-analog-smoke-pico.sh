#!/bin/sh
# Build the analog smoke application through the Pico SDK bridge and inspect
# the resulting image. This proves provider selection, the ADC and PWM ABI
# symbols, link, and UF2 structure. The analog path needs a wired board run.
set -eu

test_name=build-analog-smoke-pico
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/pico.sh"
mm_enter_root

board=pico
while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            mm_option_value "$#" "$1" "a board name"
            board=$2
            shift 2
            ;;
        -h|--help)
            echo "usage: $0 [-b|--board BOARD]"
            echo "boards: pico, pico-w, pico2-arm, pico2-w-arm, pico2-riscv, pico2-w-riscv"
            exit 0
            ;;
        *)
            mm_unknown_argument "$1"
            ;;
    esac
done

mm_pico_lane "$board"
app=analog-smoke
app_path=apps/analog-smoke
control_app=target-smoke-any
mm_pico_prepare
mm_trap_restore_host

mm_pico_banner
echo "  board  $board"
echo "  app    $app"

mm_pico_configure
mm_pico_build "$app_path/"

binary="out-target-$target/$app_path/$app"
mm_pico_verify_image "$binary"
mm_verify_provider "$binary" platform.pico.stdio 1
mm_verify_provider "$binary" platform.pico.mcu 1
mm_verify_defined "$binary" "analog" \
    mm_pico_mcu_adc_configure mm_pico_mcu_adc_read \
    mm_pico_mcu_pwm_configure mm_pico_mcu_pwm_write

mm_pico_verify_uf2 "$binary"

# Provider injection follows the application's closure. A portable application
# that does not import mm.stdio must not pay for the USB-console provider.
mm_pico_build "apps/$control_app/"
control_binary="out-target-$target/apps/$control_app/$control_app"
mm_verify_provider "$control_binary" platform.pico.stdio 0

mm_leave_host

echo "PASS: $test_name"
echo "Hardware check: GP16 through 10 kOhm to GP26, 1 uF from GP26 to ground,"
echo "  GP17 and GP0 unconnected; flash apps/$app/ and open its USB CDC console."
echo "Exit codes: 0 passed/skipped; 1-9 setup or rule failure; 10-13 the"
echo "  analog path read wrong; 14-16 temperature, release, or GPIO return."
echo "Full exit-code table: apps/$app/mm.mdy"
