#! /usr/bin/env bash

export PKG_CONFIG_PATH="/usr/lib/pkgconfig/;/lib/pkgconfig/;/usr/share/pkgconfig/"

# Delete existing buildfiles
#---------------------------------------------------
rm -rf build


# Call meson
#---------------------------------------------------
meson setup build -Dprefix="/usr"


# Build nix
#---------------------------------------------------
cd build
ninja