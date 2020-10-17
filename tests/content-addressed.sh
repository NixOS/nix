#!/usr/bin/env bash

source common.sh

drv=$(nix-instantiate --experimental-features ca-derivations ./content-addressed.nix -A rootCA --arg seed 1)
nix --experimental-features 'nix-command ca-derivations' show-derivation --derivation "$drv" --arg seed 1

testDerivation () {
    local derivationPath=$1
    local commonArgs=("--experimental-features" "ca-derivations" "./content-addressed.nix" "-A" "$derivationPath" "--no-out-link")
    local out1 out2
    out1=$(nix-build "${commonArgs[@]}" --arg seed 1)
    out2=$(nix-build "${commonArgs[@]}" --arg seed 2 "${secondSeedArgs[@]}")
    test "$out1" == "$out2"
}

testDerivation rootCA
# The seed only changes the root derivation, and not it's output, so the
# dependent derivations should only need to be built once.
secondSeedArgs=(-j0)
# Don't directly build depenentCA, that way we'll make sure we dodn't rely on
# dependent derivations always being already built.
#testDerivation dependentCA
testDerivation transitivelyDependentCA

nix-instantiate --experimental-features ca-derivations ./content-addressed.nix -A rootCA --arg seed 5
nix-collect-garbage --experimental-features ca-derivations --option keep-derivations true
