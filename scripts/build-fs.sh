#!/bin/sh
# apps/fs-smoke on any board that binds mm.fs.local: the board's own storage
# mounted at /data and checked against mm.fs.conformance. On Linux that is the
# device map's directory.0, or the working directory, and --run runs it there;
# the checks work in a scratch directory and remove it. A Pico board binds no
# mm.fs.local provider yet, so the app is unavailable there until littlefs on
# the flash region lands.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

test_name=build-fs
. "$(dirname -- "$0")/lib/common.sh"
. "$(dirname -- "$0")/lib/wrapper.sh"

wrapper_app=fs-smoke
wrapper_board=generic
wrapper_both="--control target-smoke-any"
wrapper_pico=""
wrapper_linux=""
wrapper_usage="apps/fs-smoke: mm.fs on the board's own storage, checked by mm.fs.conformance"

wrapper_after() {
    echo "Expected output: fs-smoke: 25 passed, 0 failed. On Linux --run runs it in the"
    echo "  working directory, or MM_LINUX_DEVICE_MAP's directory.0."
}

wrapper_main "$@"
