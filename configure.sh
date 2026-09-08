#!/bin/sh
#
# shell script to run the modules.cpp configure tool
#

MM_BUILD="out"
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
