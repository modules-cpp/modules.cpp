#
# shell script to run the modules.cpp model tool
#

MM_BUILD="out"
echo "Run model"
echo
${MM_BUILD}/bin/model "$@"
