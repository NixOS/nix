#! /bin/sh

set -e

rm -f current-load
touch current-load

storeExpr=$(nix-instantiate ~/nixpkgs/pkgs/system/all.nix)

export NIX_BUILD_HOOK="build-remote.pl"

../src/nix-store/nix-store -qnvvvv -j1 $storeExpr
