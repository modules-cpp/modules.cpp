#!/bin/sh
# apps/fat-smoke on any Pico board: a USB flash drive on the PIO-USB host
# port mounted as FAT at /usb and checked against mm.fs.conformance, which
# works in a scratch directory and removes it. The drive must already hold a
# FAT volume; fat-smoke never formats. Without a host port, or without a
# drive, it skips. The FatFs checkout must be provisioned with
# platforms/pico/sdk/pico-sdk/fatfs/vendor.sh.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-fat
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=fat-smoke
wrapper_board=pico2_usb_host
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_fat_attach --abi mm_pico_mcu_storage_read"
wrapper_linux=""
wrapper_usage="apps/fat-smoke: FAT on the board's USB drive, checked by mm.fs.conformance
USB host boards: --board pico_usb_host, pico2_usb_host, or rp2350_pizero_usb_host"

wrapper_after() {
    echo "Hardware check: plug a FAT-formatted USB flash drive into the host port,"
    echo "  flash apps/fat-smoke/, and open the USB CDC console. Expected output:"
    echo "  fat-smoke: 25 passed, 0 failed. Nothing outside its scratch directory"
    echo "  is touched, and the drive is never formatted."
}

wrapper_main "$@"
