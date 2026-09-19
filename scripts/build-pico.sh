#!/bin/sh
# Build an external project for a Pico board.
#
#   scripts/build-pico.sh [-b BOARD] [-r] <directory> [build arguments]
#
# The directory is an external tree, or one application inside one: a tree
# whose root manifest carries project: and points back here. It is built where
# it lives, and nothing is written into this repository. Naming a tree builds
# every application in it, and each one that reaches the SDK gets its own
# CMake bridge, so name the application's own directory to build just that
# one. Arguments after the directory are passed to build; --target is already
# passed, since a Pico build is a target build.
#
# What this adds over calling out/bin/build directly is the environment that
# call needs. The Pico SDK bridge declares picotool_DIR in its
# cmake/mm-requires.txt, and build refuses rather than searching for the
# package: docs/modules-cmake.mdy states that no package directory is found
# implicitly, because the path and the package's contents are part of the
# external cache identity. This script names the copy that
# platforms/pico/install-sdk-tools.sh installed, so the variable is declared
# rather than ambient, and passes it to one build.
#
# The lane comes from scripts/configure-pico.sh, which owns the board table
# and configures the tree. It is configured before the build and left
# configured afterwards,
# because the next thing done to an external Pico project is flashing or
# debugging it. That differs from the test scripts under scripts/, which
# restore the host configuration because a test must leave the tree as it
# found it. -r restores it here too, and ./configure does it by hand.
set -eu

script_name=build-pico.sh
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

board=pico
restore=false
external=

while [ "$#" -gt 0 ]; do
    case "$1" in
        -b|--board)
            [ "$#" -ge 2 ] || {
                echo "$script_name: $1 requires a board name" >&2; exit 64; }
            board=$2
            shift 2
            ;;
        -r|--restore-host)
            restore=true
            shift
            ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "$script_name: unknown option: $1" >&2
            exit 64
            ;;
        *)
            external=$1
            shift
            break
            ;;
    esac
done

lane=$("$(dirname -- "$0")/configure-pico.sh" -b "$board" --print) || exit $?
target=${lane%% *}
lane=${lane#* }
compiler=${lane%% *}

if [ -z "$external" ]; then
    echo "usage: $0 [-b BOARD] [-r] <directory> [build arguments]" >&2
    exit 64
fi

# Resolved before this script changes directory, so a relative path means what
# it meant in the caller's shell.
if [ ! -d "$external" ]; then
    echo "$script_name: not a directory: $external" >&2
    exit 65
fi
external=$(CDPATH= cd -- "$external" && pwd)

if [ ! -f "$external/mm.mdy" ]; then
    echo "$script_name: no manifest in $external" >&2
    echo "  an external project's root carries mm.mdy with project:" >&2
    echo "  run out/bin/sketch on it first for a sketch application" >&2
    exit 65
fi

# build takes one directory and one lane. The lane is added below, so a
# forwarded one would be a duplicate, and a forwarded path would be a second
# directory -- which is usually an application inside the tree that was named.
forwarded_lane=false
for arg in ${1+"$@"}; do
    case "$arg" in
        --target)
            forwarded_lane=true
            ;;
        --host)
            echo "$script_name: --host contradicts a Pico build" >&2
            exit 64
            ;;
        -*)
            ;;
        /*)
            echo "$script_name: two directories given:" >&2
            echo "  $external" >&2
            echo "  $arg" >&2
            echo "  build takes one; name the application's own directory" >&2
            exit 64
            ;;
        *)
            echo "$script_name: two directories given: $external and $arg" >&2
            echo "  build takes one. To build that application alone, run:" >&2
            echo "  $0 $external/$arg" >&2
            exit 64
            ;;
    esac
done

cd "$project_dir"

mm_pico_tools=${MM_PICO_TOOLS:-"$project_dir/platforms/pico/pico-sdk"}
if [ ! -d "$mm_pico_tools" ]; then
    echo "$script_name: Pico tools directory not found: $mm_pico_tools" >&2
    echo "  run platforms/pico/install-sdk-tools.sh to install them" >&2
    exit 65
fi
mm_pico_tools=$(CDPATH= cd -- "$mm_pico_tools" && pwd)

# An explicit picotool_DIR still wins, as docs/modules-cmake.mdy describes.
mm_picotool_dir=${picotool_DIR:-"$mm_pico_tools/picotool"}
if [ ! -d "$mm_picotool_dir" ]; then
    echo "$script_name: picotool package not found: $mm_picotool_dir" >&2
    echo "  run platforms/pico/install-sdk-tools.sh to install it" >&2
    exit 65
fi
mm_picotool_dir=$(CDPATH= cd -- "$mm_picotool_dir" && pwd)
if [ ! -f "$mm_picotool_dir/picotoolConfig.cmake" ] && \
   [ ! -f "$mm_picotool_dir/picotool-config.cmake" ]; then
    echo "$script_name: no CMake package in $mm_picotool_dir" >&2
    echo "  expected picotoolConfig.cmake or picotool-config.cmake" >&2
    exit 65
fi

if [ ! -f platforms/pico/sdk/pico-sdk/upstream/README.md ]; then
    echo "$script_name: Pico SDK checkout is absent" >&2
    echo "  run platforms/pico/sdk/pico-sdk/vendor.sh" >&2
    exit 65
fi

if [ ! -x out/bin/configure ] || [ ! -x out/bin/build ]; then
    echo "$script_name: host tools not found" >&2
    echo "  run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

PATH="$mm_pico_tools/bin:$PATH"
export PATH

for command_name in "$compiler" cmake; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "$script_name: required command not found: $command_name" >&2
        exit 65
    fi
done

if [ "$restore" = true ]; then
    restore_host() {
        status=$?
        trap - 0
        if ! ./configure >/dev/null 2>&1; then
            echo "$script_name: failed to restore the host configuration" >&2
            [ "$status" -ne 0 ] || status=1
        fi
        exit "$status"
    }
    trap restore_host 0
fi

echo "Pico build"
echo "  project   $project_dir"
echo "  external  $external"
echo "  board     $board"
echo "  picotool  $mm_picotool_dir"
echo

scripts/configure-pico.sh -b "$board"

if [ "$forwarded_lane" = true ]; then
    picotool_DIR="$mm_picotool_dir" out/bin/build "$external" ${1+"$@"}
else
    picotool_DIR="$mm_picotool_dir" out/bin/build --target "$external" ${1+"$@"}
fi

# Artifacts land under the external tree's root, not under the application
# directory that was named, so ascend to the outermost manifest to find them.
tree_root=$external
dir=$external
while [ -f "$dir/mm.mdy" ]; do
    tree_root=$dir
    parent=$(dirname -- "$dir")
    [ "$parent" != "$dir" ] || break
    dir=$parent
done

echo
echo "Images in $tree_root/out-target-$target:"
find "$tree_root/out-target-$target" -name '*.uf2' -type f 2>/dev/null |
    sed "s|^|  |" || true
