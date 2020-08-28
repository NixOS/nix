#!/usr/bin/env bash

source common.sh

drv=$(nix-instantiate --experimental-features ca-derivations ./content-addressed.nix -A root --arg seed 1)
nix --experimental-features 'nix-command ca-derivations' show-derivation --derivation "$drv" --arg seed 1

testDerivation () {
    local derivationPath=$1
    local commonArgs=("--experimental-features" "ca-derivations" "./content-addressed.nix" "-A" "$derivationPath" "--no-out-link")
    local out1=$(nix-build "${commonArgs[@]}" --arg seed 1)
    local out2=$(nix-build "${commonArgs[@]}" --arg seed 2 "${extraArgs[@]}")
    test $out1 == $out2
}

testDerivation root
# The seed only changes the root derivation, and not it's output, so the
# dependent derivations should only need to be built once.
extaArgs=(-j0)
testDerivation dependent
testDerivation transitivelyDependent
