#! /bin/sh -e
rm -f aclocal.m4
mkdir -p config
libtoolize --copy
aclocal
autoheader
automake --add-missing --copy
autoconf
