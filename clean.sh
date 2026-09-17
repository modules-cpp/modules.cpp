#!/bin/sh
# removes from:
#   out/          bootstrap products, installed commands, configuration, records
#   out-*/        configured build trees, out-host and any future target tree
#   gcm.cache/
#   help-dummy.o  left in the working directory by builds made before the
#                 target-option probe stopped passing -c to the C driver

rm -fr out/
rm -fr out-*/
rm -fr gcm.cache/
rm -f help-dummy.o
