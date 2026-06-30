#!/usr/bin/env bash

source common.sh

listReferences() {
    nix path-info "$1" --json --json-format 2 | \
        jq -r '.info[].references | sort | .[]'
}

# builder-rpc-v0
requireDaemonNewerThan "2.35pre20260507"

TODO_NixOS

enableFeatures 'dynamic-derivations ca-derivations'
restartDaemon

NIX_BIN_DIR="$(dirname "$(type -p nix)")"
export NIX_BIN_DIR

outPath="$(nix build -L --file ./submit-reference.nix --no-link --print-out-paths)"

mapfile -t rootRefs < <(listReferences "${outPath}")
if [[ ${#rootRefs[@]} -ne 1 ]]; then
    echo "Incorrect references for root output" >&2
    exit 1
fi

echo "${rootRefs[0]}" | grep -- "-mao$"

mapfile -t secondRefs < <(listReferences "$NIX_STORE_DIR/${rootRefs[0]}")
if [[ ${#secondRefs[@]} -ne 1 ]]; then
    echo "Incorrect references for other output" >&2
    exit 1
fi

echo "${secondRefs[0]}" | grep -- "-dependency$"
