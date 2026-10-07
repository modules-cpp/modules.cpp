#!/bin/sh
# Host CLI tests execute through mm.shell's native evaluator.
# --pico adds SDK-dependent builds with a recording flash backend.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
case "${1:-}" in ''|--pico) ;; *) echo 'usage: all.sh [--pico]' >&2; exit 64 ;; esac
for suite in run release-fetch flash-image external-apps; do
    "$root/out/bin/shell" --run "tests/scripts/$suite.sh" || exit $?
done
if [ "${1:-}" = --pico ]; then
    "$root/out/bin/shell" --run tests/scripts/external-platforms.sh -- --pico || exit $?
else
    "$root/out/bin/shell" --run tests/scripts/external-platforms.sh || exit $?
fi
