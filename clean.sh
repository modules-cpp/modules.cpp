#!/bin/sh
# removes from:
#   out/          bootstrap products, installed commands, configuration, records
#   out-*/        configured build trees, out-host and any future target tree
#   gcm.cache/

rm -fr out/
rm -fr out-*/
rm -fr gcm.cache/
