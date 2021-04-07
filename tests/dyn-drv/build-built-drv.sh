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

feats=(--experimental-features 'nix-command ca-derivations')

out1=$(nix "${feats[@]}" build -f ./text-hashed-output.nix root --no-link)

clearStore

drvDep=$(nix-instantiate "${feats[@]}" ./text-hashed-output.nix -A dependent)

out2=$(nix "${feats[@]}" build "${drvDep}!out!out" --no-link)

test $out1 == $out2
