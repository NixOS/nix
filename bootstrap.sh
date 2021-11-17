#! /bin/sh -e
cd nix
rm -f aclocal.m4
mkdir -p config
exec autoreconf -vfi
