#!/bin/sh
# Debug one already-built application through the debug tool.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_BUILD="$script_dir/out"

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
if [ ! -x "${MM_BUILD}/bin/debug" ]; then
    echo "debug: ${MM_BUILD}/bin/debug not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

exec "${MM_BUILD}/bin/debug" "$@"
