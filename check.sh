#!/bin/sh
#
# enforces docs/modules-c++20.mdy over the manifest tree
#
# Fits after the normal sequence:
#
#     ./clean.sh        remove everything generated
#     ./bootstrap.sh    build stage 0, which builds the stage 1 build tool
#     ./build.sh        build the project with it
#     ./test.sh         run every test target
#     ./check.sh        enforce docs/modules-c++20.mdy
#
# All the work is done by the check tool (tools/check), which wraps the
# installed cppcheck with the addon at tools/check/cppcheck/cpp20_rules.py;
# this script only locates the tool and passes arguments through.
#
#     ./check.sh                    the whole tree
#     ./check.sh modules/mm.mdy     a subtree
#     ./check.sh -v                 verbose, passed through to the tool
#
# Known limitation: a subtree that contains a kind:test manifest cannot be
# checked on its own, so ./check.sh tests/mm.mdy fails with "could not find
# or open any of the paths given". unit: entries are project relative while
# a subtree walk is rooted at the subtree, so they resolve one level too
# deep. Checking the whole tree covers those files; see tools/check.
#
# requires cppcheck (https://cppcheck.sourceforge.io/); this script does not
# build or run the project itself.
#
# the shebang above is required: without it "set -eu" has no effect when the
# script is executed directly
#
set -eu

cd "$(dirname "$0")"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "check: cppcheck not found; install it and re-run ./check.sh" >&2
    exit 1
fi

# Installed by build.sh. Deliberately not built here: a missing tool is a real
# error rather than a silent rebuild.
MM_TOOL="out/bin/check"

if [ ! -x "${MM_TOOL}" ]; then
    echo "check: ${MM_TOOL} not found; run ./bootstrap.sh && ./build.sh first" >&2
    exit 65
fi

exec "${MM_TOOL}" "$@"
