#! /bin/sh -e
rm -f aclocal.m4
mkdir -p config
aclocal --force --install
exec autoreconf -vfi
