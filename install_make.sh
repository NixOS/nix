#! /bin/sh -e

make
make install

#export nixstatepath=/nixstate/nix

#for i in $nixstatepath/bin/*; do
#  echo "pathing $i"
#  patchelf --set-rpath ../lib/nix/:$(patchelf --print-rpath $i) $i
#done
