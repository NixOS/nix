#!/usr/bin/env bash

source common.sh

export REMOTE_STORE="file://$cacheDir"

ensureCorrectlyCopied () {
    attrPath="$1"
    nix build --store "$REMOTE_STORE" --file ./content-addressed.nix "$attrPath"
}

testOneCopy () {
    clearStore
    clearBinaryCache

    attrPath="$1"
    nix copy --to "$REMOTE_STORE" "$attrPath" --file ./content-addressed.nix

    ensureCorrectlyCopied "$attrPath"

    # Ensure that we can copy back what we put in the store
    clearStore
    nix copy --from "$REMOTE_STORE" \
        --file ./content-addressed.nix "$attrPath" \
        --no-check-sigs
}

for attrPath in rootCA dependentCA transitivelyDependentCA dependentNonCA dependentFixedOutput; do
    testOneCopy "$attrPath"
done
