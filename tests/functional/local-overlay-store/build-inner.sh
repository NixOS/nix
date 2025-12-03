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

### Do a build in overlay store

path=$(nix-build ../hermetic.nix --arg busybox "$busybox" --arg seed 2 --store "$storeB" --no-out-link)

# Checking for path in lower layer (should fail)
expect 1 stat "$(toRealPath "$storeA/nix/store" "$path")"

# Checking for path in upper layer
stat "$(toRealPath "$storeBTop" "$path")"

# Verifying path in overlay store
nix-store --verify-path --store "$storeB" "$path"
