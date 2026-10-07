#!/bin/sh
# Wrapper dry runs evaluated by native mm.shell and compared byte for byte.
# --update rewrites fixtures only after the output change has been reviewed.
set -eu
LC_ALL=C
export LC_ALL
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
update=no
[ "${1:-}" = --update ] && update=yes
failed=0
passed=0
# Avoid a redirected compound loop: native mm.shell currently supports
# redirections on simple commands. Case names contain no whitespace.
for name in $(awk -F '|' 'NF == 4 && $1 !~ /^#/ {print $1}' "$root/tests/scripts/cases"); do
    script=$(awk -F '|' -v name="$name" '$1 == name {print $2}' "$root/tests/scripts/cases")
    arguments=$(awk -F '|' -v name="$name" '$1 == name {print $3}' "$root/tests/scripts/cases")
    expected_exit=$(awk -F '|' -v name="$name" '$1 == name {print $4}' "$root/tests/scripts/cases")
    expected="$root/tests/scripts/expected/$name.txt"
    status=0
    # Case arguments intentionally split on spaces; fixtures use simple words.
    actual=$("$root/out/bin/shell" --run "$root/scripts/$script" -- $arguments 2>&1) || status=$?
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
done
if [ "$failed" -ne 0 ]; then
    echo "FAILED ($failed/$((passed + failed)) failed)"
    exit 1
fi
echo "OK (0/$passed failed)"
