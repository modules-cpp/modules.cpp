#!/bin/sh
# apps/socket-smoke on a board whose SD socket a provider publishes through
# mm.sdcard.socket, below any file system: the card's geometry, then the last
# block written, read back, and restored. The default board is storage-linux,
# the emulated card in sdcard.img on this machine's storage-linux-<arch>, where
# --run runs it in the working directory, or wherever MM_LINUX_DEVICE_MAP's
# sdcard.path says.
# The Pico socket boards are the ones scripts/build-sd.sh lists.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-socket
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=socket-smoke
wrapper_board=storage-linux
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_mcu_spi_transfer"
wrapper_linux=""
wrapper_usage="apps/socket-smoke: the card in the board's SD socket, below any file system
Socket boards: storage-linux, rp2040_geek, rp2350_geek, rp2350_lcd_154, rp2350_touch_lcd_154, rp2350_touch_lcd_28, rp2350_pizero"

wrapper_after() {
    echo "Expected output: socket-smoke: N blocks of 512 bytes, then the last"
    echo "  block wrote, read back, and was restored. On storage-linux --run makes"
    echo "  sdcard.img, 64 MiB of zeros, in the working directory if it is missing."
    echo "  On a Pico, flash apps/socket-smoke/ and open the USB CDC console."
}

wrapper_main "$@"
