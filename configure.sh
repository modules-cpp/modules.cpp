#!/bin/sh
#
# shell script to run the modules.cpp configure tool
#

MM_BUILD="out"
echo "Run configure"
echo
${MM_BUILD}/bin/configure "$@"
