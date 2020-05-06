#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

checkBuild () {
    drvToBuild="$1"

    out1=$(nix-build ./content-addressed.nix -A "$drvToBuild" --arg seed 1 -vvv)
    out2=$(nix-build ./content-addressed.nix -A "$drvToBuild" --arg seed 2 -vvv)

    test $out1 == $out2
}

checkAll () {
    checkBuild contentAddressed
    checkBuild dependent
    checkBuild transitivelyDependent
    # Ensure that in addition to the out paths being the same, we don't rebuild
    # the derivations that don't need it
    nix-build ./content-addressed.nix --arg seed 3 |& (! grep -q "building transitively-dependent")
}

# Ensure that everything works locally
checkAll

# Same thing, but with the daemon
clearStore
startDaemon
checkAll
killDaemon
unset NIX_REMOTE

# nix-build ./content-addressed.nix --arg seed 3 |& (! grep -q "building transitively-dependent")

# clearStore

# nix-build ./content-addressed.nix --arg seed 1 \
#   --substituters "file://$cacheDir" \
#   --no-require-sigs \
#   --max-jobs 0
