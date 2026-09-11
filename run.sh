#!/bin/sh
# Run one already-built application through the run tool.

MM_BUILD="out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/run" ]; then
    echo "run: ${MM_BUILD}/bin/run not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

${MM_BUILD}/bin/run "$@"
