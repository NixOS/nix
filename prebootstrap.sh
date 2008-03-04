#! /bin/sh -e

export ACLOCAL_PATH=/home/$(whoami)/.nix-profile/share/aclocal

export AUTOCONF=autoconf
export AUTOHEADER=autoheader
export AUTOMAKE=automake

./bootstrap.sh
