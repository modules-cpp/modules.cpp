#!/bin/sh
# Build the GPIO edge smoke application through the Pico SDK bridge and
# inspect the resulting image. This proves provider selection, edge ABI symbols,
# link, and UF2 structure. Physical edge delivery needs a wired board run.
set -eu

test_name=build-gpio-edge-smoke-pico
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
app=gpio-edge-smoke
app_path=apps/gpio-edge-smoke
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
mm_verify_defined "$binary" "GPIO edge" \
    mm_pico_mcu_gpio_watch mm_pico_mcu_gpio_wait

mm_pico_verify_uf2 "$binary"

# Provider injection follows the application's closure. A portable application
# that does not import mm.stdio must not pay for the USB-console provider.
mm_pico_build "apps/$control_app/"
control_binary="out-target-$target/apps/$control_app/$control_app"
mm_verify_provider "$control_binary" platform.pico.stdio 0

mm_leave_host

echo "PASS: $test_name"
echo "Hardware check: join GPIO14 (driver) to GPIO15 (sensor), then flash"
echo "  apps/$app/ and open its USB CDC console. Remove the jumper when asked."
echo "  Replace the jumper after the manual step, including after a failure."
echo "Exit codes: 0 passed/skipped; 1-17 setup or automatic check failure;"
echo "  18 sensor stayed low; 19 confirmed missed edge; 20 inconclusive."
echo "Full exit-code table: apps/$app/mm.mdy"
