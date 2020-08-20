#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

drv=$(nix-instantiate --experimental-features ca-derivations ./content-addressed.nix --arg seed 1)
nix --experimental-features 'nix-command ca-derivations' show-derivation --derivation "$drv" --arg seed 1

out1=$(nix-build --experimental-features ca-derivations ./content-addressed.nix --arg seed 1 --no-out-link)
out2=$(nix-build --experimental-features ca-derivations ./content-addressed.nix --arg seed 2 --no-out-link)

test $out1 == $out2
