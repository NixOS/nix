#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

drv=$(nix-instantiate --experimental-features ca-derivations ./content-addressed.nix -A rootCA --arg seed 1)
nix --experimental-features 'nix-command ca-derivations' show-derivation --derivation "$drv" --arg seed 1

commonArgs=("--experimental-features" "ca-derivations" "./content-addressed.nix" "-A" "rootCA" "--no-out-link")
out1=$(nix-build "${commonArgs[@]}" ./content-addressed.nix --arg seed 1)
out2=$(nix-build "${commonArgs[@]}" ./content-addressed.nix --arg seed 2)

test $out1 == $out2

nix-instantiate --experimental-features ca-derivations ./content-addressed.nix -A rootCA --arg seed 5
nix-collect-garbage --experimental-features ca-derivations --option keep-derivations true
