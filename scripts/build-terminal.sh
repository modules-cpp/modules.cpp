#!/bin/sh
# Build the terminal tool for this Linux machine's SDL display and console.
#
# Usage: scripts/build-terminal.sh [build-linux options]
# Defaults: --board sdl --compiler clang++ --app tools/terminal
# Options include --compiler CXX, --arch aarch64|x86_64, --board BOARD,
# --run, --keep, and --dry-run. See scripts/build-linux.sh --help for details.
# The previous configuration is restored unless --keep is supplied.
# The executable is out-target-<triple>/tools/terminal/terminal.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for arg in "$@"; do
    case "$arg" in
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
    esac
done

exec sh "$script_dir/build-linux.sh" --board sdl --compiler clang++ \
    "$@" --app tools/terminal
