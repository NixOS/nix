#!/usr/bin/env bash

source common.sh

# In the corresponding nix file, we have two derivations: the first, named `hello`,
# is a normal recursive derivation, while the second, named dependent, has the
# new outputHashMode "text". Note that in "dependent", we don't refer to the
# build output of `hello`, but only to the path of the drv file. For this reason,
# we only need to:
#
# - instantiate `hello`
# - build `producingDrv`
# - check that the path of the output coincides with that of the original derivation

out1=$(nix build -f ./text-hashed-output.nix hello --no-link)

clearStore

drvDep=$(nix-instantiate ./text-hashed-output.nix -A producingDrv)

expectStderr 1 nix build "${drvDep}^out^out" --no-link | grepQuiet "Building dynamic derivations in one shot is not yet implemented"
