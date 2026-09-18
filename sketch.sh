#!/bin/sh
#
# shell script to run the modules.cpp sketch tool
#

MM_BUILD="out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/sketch" ]; then
    echo "sketch: ${MM_BUILD}/bin/sketch not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

echo "Run sketch"
echo
${MM_BUILD}/bin/sketch "$@"
