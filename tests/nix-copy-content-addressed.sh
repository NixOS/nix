#!/usr/bin/env bash

source common.sh

# Globally enable the ca derivations experimental flag
sed -i 's/experimental-features = .*/& ca-derivations/' "$NIX_CONF_DIR/nix.conf"

export REMOTE_STORE=file://$TEST_ROOT/remote_store

nix copy --to $REMOTE_STORE rootCA --file ./content-addressed.nix

checkCopy () {
    ## Ensure that the copy went right:
    # 1. The realisation file should exist
    # 2. It should be a valid json with an `outPath` field
    # 3. This field should point to the corresponding narinfo file
    drvOutput=$(basename $(nix eval --raw --file ./content-addressed.nix rootCA.drvPath))\!out
    realisationFile=$TEST_ROOT/remote_store/realisations/$drvOutput.doi

    outPathBasename=$(cat $realisationFile | jq -r .outPath)
    outPathHash=$(echo $outPathBasename | cut -d- -f1)

    test -f $TEST_ROOT/remote_store/$outPathHash.narinfo
}
checkCopy

clearStore
nix copy --from $REMOTE_STORE \
    --file ./content-addressed.nix rootCA \
    --no-check-sigs
