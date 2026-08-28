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
echo "Compile build0"
${MCCP} -v ${MM_CPPFLAGS} tools/build/main.cpp -o ${MM_BUILD}/build0
echo
#echo "Build build"
#${MM_BUILD}/build0 tools/build/mm.mdy


echo "Build build1"

mkdir -p "${MM_BUILD}/modules/mm/mdy/src"
mkdir -p "${MM_BUILD}/modules/mm/build/src"
mkdir -p "${MM_BUILD}/tools/build"

MCCP_MODULES="c++ -fmodules-ts"
MM_MODULE_FLAGS="-std=c++20 -x c++"

${MCCP_MODULES} ${MM_MODULE_FLAGS} \
    -c modules/mm/mdy/mdy.cppm \
    -o "${MM_BUILD}/modules/mm/mdy/mdy.o" || exit $?

${MCCP_MODULES} ${MM_MODULE_FLAGS} \
    -c modules/mm/mdy/src/mdy.cpp \
    -o "${MM_BUILD}/modules/mm/mdy/src/mdy.o" || exit $?

${MCCP_MODULES} ${MM_MODULE_FLAGS} \
    -c modules/mm/build/build.cppm \
    -o "${MM_BUILD}/modules/mm/build/build.o" || exit $?

${MCCP_MODULES} ${MM_MODULE_FLAGS} \
    -c modules/mm/build/src/build.cpp \
    -o "${MM_BUILD}/modules/mm/build/src/build.o" || exit $?

${MCCP_MODULES} ${MM_MODULE_FLAGS} \
    -c tools/build/build.cpp \
    -o "${MM_BUILD}/tools/build/build.o" || exit $?

c++ -std=c++20 \
    "${MM_BUILD}/modules/mm/mdy/mdy.o" \
    "${MM_BUILD}/modules/mm/mdy/src/mdy.o" \
    "${MM_BUILD}/modules/mm/build/build.o" \
    "${MM_BUILD}/modules/mm/build/src/build.o" \
    "${MM_BUILD}/tools/build/build.o" \
    -o "${MM_BUILD}/build1" || exit $?

