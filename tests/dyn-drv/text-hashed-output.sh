#!/usr/bin/env bash

source common.sh

# In the corresponding nix file, we have two derivations: the first, named root,
# is a normal recursive derivation, while the second, named dependent, has the
# new outputHashMode "text". Note that in "dependent", we don't refer to the
# build output of root, but only to the path of the drv file. For this reason,
# we only need to:
#
# - instantiate the root derivation
# - build the dependent derivation
# - check that the path of the output coincides with that of the original derivation

drv=$(nix-instantiate ./text-hashed-output.nix -A hello)
nix show-derivation "$drv"

drvProducingDrv=$(nix-instantiate ./text-hashed-output.nix -A producingDrv)
nix show-derivation "$drvProducingDrv"

out1=$(nix-build ./text-hashed-output.nix -A producingDrv --no-out-link)

nix path-info $drv --derivation --json | jq
nix path-info $out1 --derivation --json | jq

test $out1 == $drv
