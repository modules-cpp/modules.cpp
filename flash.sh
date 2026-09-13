#!/bin/sh
# Flash one already-built Pico SDK application through picotool.
set -eu

MM_BUILD="out"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Use an explicit package when supplied. Otherwise use the local Pico tools
# installed by platforms/pico/install-sdk-tools.sh.
if [ -z "${picotool_DIR:-}" ] && [ -d "$script_dir/platforms/pico/pico-sdk/picotool" ]; then
    picotool_DIR="$script_dir/platforms/pico/pico-sdk/picotool"
    export picotool_DIR
fi

if [ ! -x "${MM_BUILD}/bin/flash" ]; then
    echo "flash: ${MM_BUILD}/bin/flash not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

${MM_BUILD}/bin/flash "$@"
