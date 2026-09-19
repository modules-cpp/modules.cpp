#!/bin/sh
# Configure this project for a Pico board.
#
#   scripts/configure-pico.sh [-b|--board BOARD] [--print]
#
# Boards: pico, pico-w, pico2-arm, pico2-w-arm, pico2-riscv, pico2-w-riscv.
#
# Two calls to configure, not one. The first resolves the configuration back
# to the host alone, so a board, SDK, or compiler left by a previous lane
# cannot survive into this one; the second names the lane. A single call with
# every flag is what the test scripts under scripts/ do, and is enough when
# the tree was already host-configured.
#
# --print writes "target compiler sdk" for the board and configures nothing.
# It is how scripts/build-pico.sh learns the lane without repeating the table
# below: one file owns which board means which target, compiler, and SDK.
#
# Configuring is separate from building because the lane outlives one build:
# out/bin/build, flash, debug, and run all read the configuration this wrote.
set -eu

script_name=configure-pico.sh
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

board=pico
print_only=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            [ "$#" -ge 2 ] || {
                echo "$script_name: $1 requires a board name" >&2; exit 64; }
            board=$2
            shift 2
            ;;
        -p|--print)
            print_only=true
            shift
            ;;
        -h|--help)
            sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "$script_name: unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

# The one table. Everything else that needs to know what a board is asks here.
case "$board" in
    pico|pico-w|pico2-arm|pico2-w-arm)
        target=arm-none-eabi
        compiler=arm-none-eabi-gcc
        sdk=pico-arm
        ;;
    pico2-riscv|pico2-w-riscv)
        target=riscv32-pico-elf
        compiler=riscv32-pico-elf-gcc
        sdk=pico-riscv
        ;;
    *)
        echo "$script_name: unsupported board: $board" >&2
        echo "  boards: pico, pico-w, pico2-arm, pico2-w-arm, pico2-riscv," >&2
        echo "          pico2-w-riscv" >&2
        exit 64
        ;;
esac

if [ "$print_only" = true ]; then
    echo "$target $compiler $sdk"
    exit 0
fi

cd "$project_dir"

if [ ! -x out/bin/configure ]; then
    echo "$script_name: host tools not found" >&2
    echo "  run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

# Back to the host first, so nothing of a previous lane is inherited.
./configure --compiler g++

./configure \
    --target "$target" \
    --compiler "$compiler" \
    --sdk "$sdk" \
    --board "$board" \
    --build debug
