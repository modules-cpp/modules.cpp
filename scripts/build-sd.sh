#!/bin/sh
# apps/sd-smoke on a board whose TF socket a provider publishes through
# mm.sdcard.socket -- rp2040_geek, rp2350_geek, rp2350_lcd_154,
# rp2350_touch_lcd_154 in SPI mode; rp2350_touch_lcd_28, rp2350_pizero, and
# rp2350_pizero_usb_host in 4-bit SD mode over PIO; and storage-linux, the
# emulated card in sdcard.img: the card's FAT
# volume mounted at /sd and checked against mm.fs.conformance, which works in
# a scratch directory and removes it. sd-smoke never formats. The FatFs
# checkout must be provisioned with platforms/pico/sdk/pico-sdk/fatfs/vendor.sh
# for Pico, platforms/linux/fatfs/vendor.sh for Linux.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-sd
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=sd-smoke
wrapper_board=rp2040_geek
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_fat_attach --abi mm_pico_mcu_spi_transfer"
wrapper_linux=""
wrapper_usage="apps/sd-smoke: FAT on the card in the board's TF socket, checked by mm.fs.conformance
Socket boards: rp2040_geek, rp2350_geek, rp2350_lcd_154, rp2350_touch_lcd_154, rp2350_touch_lcd_28, rp2350_pizero, storage-linux"

wrapper_after() {
    echo "Hardware check: put a FAT-formatted card in the TF socket, flash"
    echo "  apps/sd-smoke/, and open the USB CDC console. Expected output:"
    echo "  sd-smoke: 25 passed, 0 failed. Nothing outside its scratch directory"
    echo "  is touched, and the card is never formatted."
}

wrapper_main "$@"
