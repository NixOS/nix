#! /bin/sh -e
rm -f aclocal.m4
mkdir -p config
autoreconf -vfi
cd perl
autoreconf -vfi
cd ..
