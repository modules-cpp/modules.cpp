#!/bin/sh
#
# shell script to run the modules.cpp json tool
#

MM_BUILD="out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/json" ]; then
    echo "json: ${MM_BUILD}/bin/json not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

${MM_BUILD}/bin/json "$@"
