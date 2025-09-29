#!/usr/bin/env bash

set -eu -o pipefail

source common.sh

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR
unset NIX_STATE_DIR

setupStoreDirs

initLowerStore

mountOverlayfs

export NIX_REMOTE="$storeB"
# shellcheck disable=SC2034
stateB="$storeBRoot/nix/var/nix"
hermetic=$(nix-build ../hermetic.nix --no-out-link --arg busybox "$busybox" --arg withFinalRefs true --arg seed 2)
input1=$(nix-build ../hermetic.nix --no-out-link --arg busybox "$busybox" --arg withFinalRefs true --arg seed 2 -A passthru.input1 -j0)
input2=$(nix-build ../hermetic.nix --no-out-link --arg busybox "$busybox" --arg withFinalRefs true --arg seed 2 -A passthru.input2 -j0)
input3=$(nix-build ../hermetic.nix --no-out-link --arg busybox "$busybox" --arg withFinalRefs true --arg seed 2 -A passthru.input3 -j0)

# Can't delete because referenced
expectStderr 1 nix-store --delete "$input1" | grepQuiet "Cannot delete path"
expectStderr 1 nix-store --delete "$input2" | grepQuiet "Cannot delete path"
expectStderr 1 nix-store --delete "$input3" | grepQuiet "Cannot delete path"

# These same paths are referenced in the lower layer (by the seed 1
# build done in `initLowerStore`).
expectStderr 1 nix-store --store "$storeA" --delete "$input2" | grepQuiet "Cannot delete path"
expectStderr 1 nix-store --store "$storeA" --delete "$input3" | grepQuiet "Cannot delete path"

# Can delete
nix-store --delete "$hermetic"

# Now unreferenced in upper layer, can delete
nix-store --delete "$input3"
nix-store --delete "$input2"
