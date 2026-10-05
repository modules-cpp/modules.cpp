#!/bin/sh
# apps/fs-smoke on any board that binds mm.fs.local: the board's own storage
# mounted at /data and checked against mm.fs.conformance. On a Pico board that
# is littlefs on the flash region at the top of program flash, formatted on
# first mount; the littlefs checkout must be provisioned with
# platforms/pico/sdk/pico-sdk/littlefs/vendor.sh. On Linux it is the device
# map's directory.0, or the working directory, and --run runs it there. The
# checks work in a scratch directory and remove it.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-fs
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=fs-smoke
wrapper_board=pico
wrapper_both="--control target-smoke-any"
wrapper_pico="--abi mm_pico_lfs_attach --abi mm_pico_mcu_flash_region_program"
wrapper_linux=""
wrapper_usage="apps/fs-smoke: mm.fs on the board's own storage, checked by mm.fs.conformance"

wrapper_after() {
    echo "Expected output: fs-smoke: 25 passed, 0 failed. On a Pico, flash"
    echo "  apps/fs-smoke/ and open the USB CDC console; the first run formats the"
    echo "  flash region. On Linux (--board generic) --run runs it in the working"
    echo "  directory, or MM_LINUX_DEVICE_MAP's directory.0."
}

wrapper_main "$@"
