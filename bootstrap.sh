#! /bin/sh -e
rm -f aclocal.m4
mkdir -p config
libtoolize --copy -f
aclocal
autoheader -f
automake --add-missing --copy -f
autoconf -f
