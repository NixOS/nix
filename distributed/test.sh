#! /bin/sh

set -e

storeExpr=$(nix-instantiate ~/nixpkgs/pkgs/system/test.nix)

export NIX_BUILD_HOOK="build-remote.pl"

../src/nix-store/nix-store -qnvvvv -j0 $storeExpr
