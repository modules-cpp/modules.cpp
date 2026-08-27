#!/bin/sh
# runs tests

echo test build0
expected='modules.cpp build tool
./out/build0
no arguments'
status=$?

actual=$(./out/build0 2>&1)
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build0 tool output"
else
    echo "FAIL: unexpected output" >&2
    exit 1
fi

echo test build

expected='modules.cpp build tool
./out/build
no arguments'
status=$?
actual=$(./out/build 2>&1)
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build tool output"
else
    echo "FAIL: unexpected output" >&2
    exit 1
fi
