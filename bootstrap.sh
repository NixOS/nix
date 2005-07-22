#! /bin/sh -e
mkdir -p config
libtoolize --copy
aclocal
autoheader
automake --add-missing --copy
autoconf
