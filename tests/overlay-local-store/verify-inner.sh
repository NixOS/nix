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

#path=$(nix-store --store "$storeB" --add ../dummy)

path=$(nix-build --store $storeB ../hermetic.nix --arg busybox "$busybox" --arg seed 1)

inputDrvPath=$(find "$storeA" -name "*-hermetic-input-1.drv")
rm -v "$inputDrvPath"

#tree "$TEST_ROOT"

#rm -v "$TEST_ROOT/store-a/$path"

nix-store --store "$storeB" --verify

echo "SUCCESS"
