#!/bin/sh
# runs tests
set -eu

verbose=false
lane=""
compile_only=false
for arg in "$@"; do
    case "$arg" in
        -v|--verbose) verbose=true ;;
        --host|--target)
            if [ -n "$lane" ]; then
                echo "test: lane option may be given only once" >&2
                exit 64
            fi
            lane=$arg
            ;;
        --compile-only)
            if [ "$compile_only" = true ]; then
                echo "test: --compile-only may be given only once" >&2
                exit 64
            fi
            compile_only=true
            ;;
        *)
            echo "usage: test.sh [-v|--verbose] [--host|--target] [--compile-only]" >&2
            exit 64
            ;;
    esac
done

# A suite the selected lane cannot build is skipped here rather than failing the
# run. The test tool is right to reject a manifest the user named explicitly
# (docs/modules-test.mdy), but this script names all of them, so for it an
# unbuildable suite is a lane fact rather than a fault.
#
# The test tool returns 77 only when the requested suite is unavailable in the
# selected lane. That lets output remain connected to the terminal while this
# wrapper classifies the result without parsing diagnostic text.
skipped_suites=0

run_test_target() {
    status=0
    if [ "$verbose" = true ] && [ "$compile_only" = true ]; then
        out/bin/test -v ${lane:+"$lane"} --compile-only "$1" || status=$?
    elif [ "$verbose" = true ]; then
        out/bin/test -v ${lane:+"$lane"} "$1" || status=$?
    elif [ "$compile_only" = true ]; then
        out/bin/test ${lane:+"$lane"} --compile-only "$1" || status=$?
    else
        out/bin/test ${lane:+"$lane"} "$1" || status=$?
    fi
    if [ "$status" -eq 77 ]; then
        skipped_suites=$((skipped_suites + 1))
        status=0
    fi
    return $status
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

expected='Usage: build0 [-h|--help] [build1]'
expected_status=0
status=0
actual=$(./out/build0 --help 2>&1) || status=$?
check "build0 help output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test build1
echo
expected='Usage: build [-v|--verbose] [-h|--help] [--host | --target] [manifest]'
expected_status=0
status=0
actual=$(./out/build1 -h 2>&1) || status=$?
check "build1 help output" "$expected_status" "$status" "$expected" "$actual"

echo
echo test build
echo
expected='Usage: build [-v|--verbose] [-h|--help] [--host | --target] [manifest]'
expected_status=0
status=0
actual=$(./out/bin/build -h 2>&1) || status=$?
check "build help output" "$expected_status" "$status" "$expected" "$actual"

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

expected='Usage: main [-v|--verbose] [-h|--help]'
expected_status=0
status=0
actual=$(./out/bin/main --help 2>&1) || status=$?
check "app main help output" "$expected_status" "$status" "$expected" "$actual"

expected='modules.cpp main application
  verbose true'
expected_status=0
status=0
actual=$(./out/bin/main --verbose 2>&1) || status=$?
check "app main verbose output" "$expected_status" "$status" "$expected" "$actual"

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
run_test_target tests/mm/debug/ || exit $?
run_test_target tests/mm/mdy/ || exit $?
run_test_target tests/mm/shell/ || exit $?
run_test_target tests/mm/model/ || exit $?
run_test_target tests/mm/run/ || exit $?

if [ "$skipped_suites" -gt 0 ]; then
    echo
    echo "$skipped_suites suite(s) skipped: not buildable in the selected lane"
fi
