#!/bin/sh
# Preserve the caller's directory; external cleanup is manifest-aware and offline.
LC_ALL=C
export LC_ALL
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$script_dir/out/bin/clean" ]; then
    exec "$script_dir/out/bin/clean" "$@"
fi
# Before bootstrap there is no clean tool. Preserve the historical root reset
# only at this installation's root, never in an arbitrary caller directory.
if [ "$#" -ne 0 ] || [ "$PWD" != "$script_dir" ]; then
    echo "clean: installed tool missing; bootstrap the installation first" >&2
    exit 65
fi
rm -fr out/
rm -fr out-*/
rm -fr gcm.cache/
rm -f help-dummy.o
