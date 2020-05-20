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

checkAllWithDaemon () {
    clearStore
    startDaemon
    checkAll
    killDaemon
    unset NIX_REMOTE
}

checkRemoteCache () {
    # Push all the paths to the cache
    clearStore
    nix-build content-addressed.nix \
        --arg seed 1 \
        --post-build-hook $PWD/push-to-store.sh

    # Rebuild with a clean store using the remote cache, and ensure that we
    # don't really build anything
    clearStore
    nix-build ./content-addressed.nix --arg seed 2 \
    --substituters "file://$cacheDir" \
    --no-require-sigs \
        |& (! grep -q "building transitively-dependent")
}

checkAll
checkAllWithDaemon
checkRemoteCache
