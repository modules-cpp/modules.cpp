#!/bin/sh
#
# shell script to build modules.cpp with default modular c++ compiler
#
#     ./build.sh                    the whole project from mm.mdy
#     ./build.sh modules/mm.mdy     a subtree
#     ./build.sh -v                 verbose, passed through to the tool
#

MM_BUILD="out"
echo "Build in ${MM_BUILD}"
echo
echo "Build all"
"${MM_BUILD}/bin/build" "$@"
