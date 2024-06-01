#!/bin/sh
set -e
. ./config.sh

rm -f TAGS
 
for PROJECT in $PROJECTS; do
  (cd $PROJECT && $MAKE clean)
done
 
rm -rf sysroot
rm -rf isodir
rm -rf myos.iso
