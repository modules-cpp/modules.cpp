#!/bin/sh
# runs tests
echo
echo test build0
echo
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

echo
echo test build1
echo
expected='build: not an mm.mdy manifest: -h'
status=$?
actual=$(./out/build1 -h    2>&1)
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build tool output"
else
    echo "FAIL: unexpected output" >&2
    exit 1
fi

echo
echo test app main
echo
expected=''
status=$?
actual=$(./out/apps/main/main 2>&1)
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build tool output"
else
    echo "FAIL: unexpected ou   tput" >&2
    exit 1
fi

echo
echo test app mdy
echo
expected='=== METADATA EXTRACTED ===
kind -> file
mm -> 0.1
name -> sample.mdy

=== BODY CONTENT TRAVERSAL ===
Heading1: modules.cpp C++20 Modules
Heading2: Features
Text: Faster compilation speeds than headers
Text: True logical separation of interface code
Heading3: Rules
Text: Modules replace old header-file macro include frameworks entirely.'
status=$?
actual=$(./out/apps/mdy/mdy -s 2>&1)
printf 'expected: [%s]\n' "$expected" >&2
printf 'actual:   [%s]\n' "$actual" >&2

if [ "$actual" = "$expected" ]; then
    echo "PASS: build tool output"
else
    echo "FAIL: unexpected output" >&2
    exit 1
fi
