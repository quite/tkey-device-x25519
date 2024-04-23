#!/bin/bash
set -eu

clang --version

LIBS="v0.0.2"
LIBSD=../tkey-libs

if [[ ! -e $LIBSD ]]; then
  git clone --branch=$LIBS https://github.com/tillitis/tkey-libs $LIBSD
else
  printf "NOTE: building with existing %s, possibly not a clean clone!\n" $LIBSD
fi
(cd $LIBSD; pwd; git describe --dirty --long --always)
make -C $LIBSD -j

make clean
make
