#!/bin/sh
set -e
. ./headers.sh

if $(command -v etags &> /dev/null) && $(command -v find &> /dev/null)
then
    find ${PROJECTS} -name "*.[chCH]" -print | etags -
fi

for PROJECT in $PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install)
done
