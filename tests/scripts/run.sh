#!/bin/sh
# The build scripts' own tests: every case in tests/scripts/cases runs a
# platform script with --dry-run, which resolves the lane and derives every
# check from the manifests without touching the tree or needing a toolchain,
# and compares what it prints and how it exits against
# tests/scripts/expected/<name>.txt.
#
#   tests/scripts/run.sh [--update]
#
# --update rewrites the expected files from the current output, for a change
# whose new output has been reviewed.

# Sort, compare, and match bytes, and keep tool messages untranslated,
# whatever the caller's locale.
LC_ALL=C
export LC_ALL

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

update=no
[ "${1:-}" = --update ] && update=yes

failed=0
passed=0
while IFS='|' read -r name script arguments expected_exit; do
    case "$name" in ''|'#'*) continue ;; esac
    expected="tests/scripts/expected/$name.txt"
    status=0
    # shellcheck disable=SC2086
    actual=$(sh "scripts/$script" $arguments 2>&1) || status=$?
    actual=$(printf '%s\nexit %s\n' "$actual" "$status")
    if [ "$update" = yes ]; then
        printf '%s\n' "$actual" > "$expected"
    fi
    if [ "$status" != "$expected_exit" ]; then
        echo "  [fail] $name: exit $status, expected $expected_exit"
        failed=$((failed + 1))
    elif [ ! -f "$expected" ] || [ "$(cat "$expected")" != "$actual" ]; then
        echo "  [fail] $name: output differs from $expected"
        printf '%s\n' "$actual" | diff "$expected" - | sed 's/^/    /' || true
        failed=$((failed + 1))
    else
        echo "  [pass] $name"
        passed=$((passed + 1))
    fi
done < tests/scripts/cases

if [ "$failed" -ne 0 ]; then
    echo "FAILED ($failed/$((passed + failed)) failed)"
    exit 1
fi
echo "OK (0/$passed failed)"
