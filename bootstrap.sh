#! /bin/sh -e
mkdir -p config
libtoolize --force --copy
aclocal
autoheader
automake --add-missing --copy
autoconf
