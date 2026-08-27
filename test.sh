#!/bin/sh
# runs tests

expected='modules.cpp build tool'
actual=$(./out/build) || exit $?
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build tool output"
else
    echo "FAIL: unexpected output" >&2
    exit 1
fi