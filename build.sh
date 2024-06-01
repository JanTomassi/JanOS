#!/bin/sh
set -e
. ./headers.sh

find ${PROJECTS} -name "*.[chCH]" -print | etags -

for PROJECT in $PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install)
done
