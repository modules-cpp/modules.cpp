#!/bin/sh
# apps/mcu-shell, the interactive board shell, with its file commands over the
# board's own storage at /data: littlefs on the flash region on a Pico,
# formatted the first time, and the device map's directory on Linux. --sd
# is the build option that adds the board's SD card at /sd: it builds
# apps/mcu-shell-sd instead, which carries FatFs and the card driver, on a
# board whose socket a provider publishes through mm.sdcard.socket. The
# littlefs checkout, and for --sd the FatFs one, must be provisioned with
# their vendor.sh scripts.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-shell
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=mcu-shell
wrapper_board=pico
wrapper_both="--control mcu-shell-size-control"
wrapper_pico="--abi mm_pico_lfs_attach"
wrapper_linux=""
wrapper_usage="apps/mcu-shell: the board shell with file commands on /data; --sd adds the SD card at /sd"

# --sd picks the application and is not passed on.
set -- "$@" --mm-end
while [ "$1" != --mm-end ]; do
    if [ "$1" = --sd ]; then
        wrapper_app=mcu-shell-sd
        wrapper_board=rp2040_geek
        wrapper_pico="$wrapper_pico --abi mm_pico_fat_attach"
    else
        set -- "$@" "$1"
    fi
    shift
done
shift

wrapper_after() {
    echo "Hardware check: flash the image and open the USB CDC console, then:"
    echo "  write /data/hello.txt hi; cat /data/hello.txt; ls /data; df"
    if [ "$wrapper_app" = mcu-shell-sd ]; then
        echo "  and with a FAT card in the socket: ls /sd; sd mounts one inserted later."
    fi
}

wrapper_main "$@"
