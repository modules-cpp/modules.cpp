#!/bin/sh
# Run one already-built application through the run tool.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

MM_BUILD="out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/run" ]; then
    echo "run: ${MM_BUILD}/bin/run not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

${MM_BUILD}/bin/run "$@"
