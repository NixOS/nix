#!/usr/bin/env bash

set -eu -o pipefail

set -x

source common.sh

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR
unset NIX_STATE_DIR

setupStoreDirs

initLowerStore

mountOverlayfs

buildInStore () {
    nix-build --store "$1" ../hermetic.nix --arg busybox "$busybox" --arg seed 1 --no-out-link
}

triggerStaleFileHandle () {
    # Arrange it so there are duplicate paths
    nix-store --store "$storeA" --gc  # Clear lower store
    buildInStore "$storeB"  # Build into upper layer first
    buildInStore "$storeA"  # Then build in lower store

    # Duplicate paths mean GC will have to delete via upper layer
    nix-store --store "$storeB" --gc

    # Clear lower store again to force building in upper layer
    nix-store --store "$storeA" --gc

    # Now attempting to build in upper layer will fail
    buildInStore "$storeB"
}

# Without remounting, we should encounter errors
expectStderr 1 triggerStaleFileHandle | grepQuiet 'Stale file handle'

# Configure remount-hook and reset OverlayFS
storeB="$storeB&remount-hook=$PWD/remount.sh"
remountOverlayfs

# Now it should succeed
triggerStaleFileHandle
