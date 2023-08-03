#!/usr/bin/env bash

set -eu -o pipefail

set -x

source common.sh

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR
unset NIX_STATE_DIR

storeDirs

initLowerStore

mountOverlayfs

### Do a redundant add

# upper layer should not have it
expect 1 stat $(toRealPath "$storeBTop/nix/store" "$path")

path=$(nix-store --store "$storeB" --add ../dummy)

# lower store should have it from before
stat $(toRealPath "$storeA/nix/store" "$path")

# upper layer should still not have it (no redundant copy)
expect 1 stat $(toRealPath "$storeBTop" "$path")
