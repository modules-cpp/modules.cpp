#!/bin/sh
# runs tests
set -eu

verbose=false
for arg in "$@"; do
    case "$arg" in
        -v|--verbose) verbose=true ;;
        *)
            echo "usage: test.sh [-v|--verbose]" >&2
            exit 64
            ;;
    esac
done

run_test_target() {
    if [ "$verbose" = true ]; then
        out/bin/test -v "$1"
    else
        out/bin/test "$1"
    fi
}

check() {
    # check <label> <expected status> <actual status> <expected output> <actual output>
    label=$1
    expected_status=$2
    actual_status=$3
    expected_output=$4
    actual_output=$5

    printf 'expected: [%s]\n' "$expected_output" >&2
    printf 'actual:   [%s]\n' "$actual_output" >&2
    printf 'expected status: %s\n' "$expected_status" >&2
    printf 'actual status:   %s\n' "$actual_status" >&2

    if [ "$actual_output" != "$expected_output" ]; then
        echo "FAIL: $label: unexpected output" >&2
        exit 1
    fi
    if [ "$actual_status" != "$expected_status" ]; then
        echo "FAIL: $label: unexpected exit status" >&2
        exit 1
    fi
    echo "PASS: $label"
}

echo
echo test build0
echo
expected='modules.cpp build tool
./out/build0
no arguments'
expected_status=0
status=0
actual=$(./out/build0 2>&1) || status=$?
check "build0 tool output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test build1
echo
expected='build: unknown option: -h'
expected_status=64
status=0
actual=$(./out/build1 -h 2>&1) || status=$?
check "build tool output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test build
echo
expected='build: unknown option: -h'
expected_status=64
status=0
actual=$(./out/bin/build -h 2>&1) || status=$?
check "build tool output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test configure
echo
expected='configure: unknown option: --release'
expected_status=64
status=0
actual=$(./out/bin/configure --release 2>&1) || status=$?
check "configure unknown option" "$expected_status" "$status" "$expected" "$actual"

echo
echo test app main
echo
expected=''
expected_status=0
status=0
actual=$(./out/bin/main 2>&1) || status=$?
check "app main output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test app mdy
echo
expected='=== METADATA EXTRACTED ===
kind -> file
mm -> 1.0
name -> sample.mdy

=== BODY CONTENT TRAVERSAL ===
Heading1: modules.cpp C++20 Modules
Heading2: Features
Text: Faster compilation speeds than headers
Text: True logical separation of interface code
Heading3: Rules
Text: Modules replace old header-file macro include frameworks entirely.'
expected_status=0
status=0
actual=$(./out/bin/mdy -s 2>&1) || status=$?
check "app mdy output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test test
echo
run_test_target tests/mm/build/ || exit $?
run_test_target tests/mm/configure/ || exit $?
run_test_target tests/mm/mdy/ || exit $?
run_test_target tests/mm/shell/ || exit $?
run_test_target tests/mm/model/ || exit $?
