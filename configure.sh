#!/bin/sh
#
# shell script to run the modules.cpp configure tool
#

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MM_BUILD="$script_dir/out"
echo "Run configure"
echo

if [ -x "${MM_BUILD}/bin/configure" ]; then
    exec "${MM_BUILD}/bin/configure" "$@"
fi

if [ -x "${MM_BUILD}/configure1" ]; then
    exec "${MM_BUILD}/configure1" "$@"
fi

echo "configure: no configure tool; run ./bootstrap.sh first" >&2
exit 65
