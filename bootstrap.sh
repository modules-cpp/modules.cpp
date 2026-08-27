# Pawel Wodnicki (C) 2026
# 32bitmicro LLC (C) 2026
#
# shell script to bootstrap modules.cpp with host c++-20 compiler
#

MCCP="c++"
MCCP_VERSION=`$MCCP --version`
echo
echo "Compiler version"
echo
echo ${MCCP_VERSION}
echo 
MM_BUILD="out/"
echo "Build in ${MM_BUILD}"
echo
MM_CPPFLAGS="-std=c++20"
echo "Flags ${MM_CPPFLAGS}"
echo
mkdir ${MM_BUILD}
${MCCP}  tools/build/main.cpp -o ${MM_BUILD}/build
echo