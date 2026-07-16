#!/usr/bin/env bash

source common.sh

signIfNeeded

nix-store --generate-binary-cache-key cache1.example.org "$TEST_ROOT/sk1" "$TEST_ROOT/pk1"
pk1=$(cat "$TEST_ROOT/pk1")
nix-store --generate-binary-cache-key cache2.example.org "$TEST_ROOT/sk2" "$TEST_ROOT/pk2"
pk2=$(cat "$TEST_ROOT/pk2")

export REMOTE_STORE="file://$cacheDir"

ensureCorrectlyCopied () {
    attrPath="$1"
    nix build --store "$REMOTE_STORE" --file ./content-addressed.nix "$attrPath"
}

testOneCopy () {
    clearStore
    clearBinaryCache

    attrPath="$1"

    drvPath=$(nix eval --raw --file ./content-addressed.nix "$attrPath.drvPath")
    nix build --no-link --secret-key-files "$TEST_ROOT/sk1" "$drvPath^*"

    nix copy -vvvv --to "$REMOTE_STORE" "$drvPath^*"

    ensureCorrectlyCopied "$attrPath"

    # Copies with the wrong key should fail
    clearStore
    expectStderr 1 nix copy --from "$REMOTE_STORE" \
        --file ./content-addressed.nix "$attrPath" \
        --trusted-public-keys "$pk2" \
        | grepQuiet 'lacks a signature by a trusted key'

    # Same logic applies to nix build
    clearStore
    expectStderr 1 nix build --file ./content-addressed.nix "$attrPath" \
        --substitute -j0 --substituters "$REMOTE_STORE" \
        --trusted-public-keys "$pk2" \
        --no-link \
        | grepQuiet 'not signed by any of the keys'

    # Copies with the correct key should succeed
    clearStore
    nix copy --from "$REMOTE_STORE" \
        --file ./content-addressed.nix "$attrPath" \
        --trusted-public-keys "$pk1"

    # Same logic applies to nix build
    clearStore
    nix build --file ./content-addressed.nix "$attrPath" \
        --substitute -j0 --substituters "$REMOTE_STORE" \
        --trusted-public-keys "$pk1" \
        --no-link
}

for attrPath in rootCA dependentCA transitivelyDependentCA dependentNonCA; do
    testOneCopy "$attrPath"
done
