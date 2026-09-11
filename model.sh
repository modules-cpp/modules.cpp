#!/bin/sh
#
# shell script to run the modules.cpp model tool
#

MM_BUILD="out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/model" ]; then
    echo "model: ${MM_BUILD}/bin/model not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

echo "Run model"
echo
${MM_BUILD}/bin/model "$@"
