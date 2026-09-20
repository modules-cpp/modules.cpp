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
# Apple Clang does not accept GCC's -fmodules-ts.  Keep the historical
# defaults on other hosts, while selecting the Clang module flags on macOS.
# The override is passed to the already-built tool rather than relying on
# CXX/CXXFLAGS environment variables, so an invocation of ./build works on a
# clean Darwin checkout as well.
case "$(uname -s)" in
    Darwin)
        exec "${MM_BUILD}/bin/build" --compiler clang++ \
            --flags "-std=c++20 -fprebuilt-module-path=${MM_BUILD}/module-cache" "$@"
        ;;
    *)
        exec "${MM_BUILD}/bin/build" "$@"
        ;;
esac
