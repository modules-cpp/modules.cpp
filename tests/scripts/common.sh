# Shared helpers for native mm.shell CLI regression tests.
# Source after setting root. Each suite owns one mktemp directory.
set -eu
LC_ALL=C
export LC_ALL
work=$(mktemp -d "${TMPDIR:-/tmp}/mm-shell-tests.XXXXXX")
checks=0
trap 'rm -rf "$work"' EXIT INT TERM
# Native command-substitution children inherit trap metadata. Compiler probes
# reset EXIT in their child state so they cannot clean the parent fixture.
fail() {
    echo "FAIL: $*" >&2
    if [ -f "$work/command.log" ]; then cat "$work/command.log" >&2; fi
    exit 1
}
pass() {
    checks=$((checks + 1))
    echo "  [pass] $*"
}
expect_status() {
    expected=$1
    shift
    status=0
    "$@" > "$work/command.log" 2>&1 || status=$?
    [ "$status" -eq "$expected" ] || fail "exit $status, expected $expected: $*"
    pass "$* (exit $expected)"
}
expect_failure() {
    status=0
    "$@" > "$work/command.log" 2>&1 || status=$?
    [ "$status" -ne 0 ] || fail "unexpected success: $*"
    pass "$* refused"
}
contains() {
    rg -q -- "$2" "$1" || fail "$1 lacks $2"
    pass "$1 contains $2"
}
absent() {
    [ ! -e "$1" ] || fail "unexpected output: $1"
    pass "$1 absent"
}
same() {
    cmp -s "$1" "$2" || fail "$1 differs from $2"
    pass "$1 unchanged"
}
