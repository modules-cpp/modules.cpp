#!/bin/sh
#
# shell script to build modules.cpp with default modular c++ compiler
#
#     ./build.sh                    the whole project from mm.mdy
#     ./build.sh modules/mm.mdy     a subtree
#     ./build.sh -v                 verbose, passed through to the tool
#

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_BUILD="$script_dir/out"
echo "Build in ${MM_BUILD}"
echo
echo "Build all"
if [ ! -x "${MM_BUILD}/bin/build" ]; then
    echo "build: ${MM_BUILD}/bin/build not found; bootstrap the installation first" >&2
    exit 65
fi
exec "${MM_BUILD}/bin/build" "$@"
