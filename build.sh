#
# shell script to build modules.cpp with default modular c++ compiler
#

MCCP="c++ -fmodules-ts "
MCCP_VERSION=`${MCCP} --version`
echo
echo "Compiler version"
echo
echo ${MCCP_VERSION}
echo 
MM_BUILD="out"
echo "Build in ${MM_BUILD}"
echo
MM_CPPFLAGS="-std=c++20 -x c++"
echo "Flags ${MM_CPPFLAGS}"
echo
mkdir -p ${MM_BUILD}
echo
echo "Compile modules"
echo "Compile module mm::app"
mkdir -p ${MM_BUILD}/modules/mm/app
${MCCP} -v ${MM_CPPFLAGS} -c modules/mm/app/app.cppm -o ${MM_BUILD}/modules/mm/app/app.o
echo "Compile module mm:mdy"
mkdir -p ${MM_BUILD}/modules/mm/mdy/src
${MCCP} -v ${MM_CPPFLAGS} -c modules/mm/mdy/mdy.cppm -o ${MM_BUILD}/modules/mm/mdy/mdy.o
${MCCP} -v ${MM_CPPFLAGS} -c modules/mm/mdy/src/mdy.cpp -o ${MM_BUILD}/modules/mm/mdy/src/mdy.o
echo
echo "Compile app main"
mkdir -p ${MM_BUILD}/apps/main
${MCCP} -v ${MM_CPPFLAGS} -c apps/main/main.cpp -o ${MM_BUILD}/apps/main/main.o
echo "Link main"
MM_LDFLAGS="-std=c++20"
${MCCP} -v ${MM_LDFLAGS} ${MM_BUILD}/apps/main/main.o ${MM_BUILD}/modules/mm/app/app.o -o ${MM_BUILD}/apps/main/main
echo
echo
echo "Compile app mdy"
mkdir -p ${MM_BUILD}/apps/mdy
${MCCP} -v ${MM_CPPFLAGS} -c apps/mdy/mdy.cpp -o ${MM_BUILD}/apps/mdy/mdy.o
echo "Link main"
MM_LDFLAGS="-std=c++20"
${MCCP} -v ${MM_LDFLAGS} ${MM_BUILD}/modules/mm/mdy/src/mdy.o ${MM_BUILD}/modules/mm/mdy/mdy.o ${MM_BUILD}/apps/mdy/mdy.o -o ${MM_BUILD}/apps/mdy/mdy
echo

#echo "Install main"
#mkdir -p ${MM_BUILD}/bin
#cp -pr ${MM_BUILD}/apps/main/main ${MM_BUILD}/bin