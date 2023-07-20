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

# Add something to the overlay store
overlayPath=$(addTextToStore "$storeB" "overlay-file" "Add to overlay store")
stat "$storeBRoot/$overlayPath"

# Now add something to the lower store
lowerPath=$(addTextToStore "$storeA" "lower-file" "Add to lower store")
stat "$storeVolume/store-a/$lowerPath"

# Remount overlayfs to ensure synchronization
remountOverlayfs

# Path should be accessible via overlay store
stat "$storeBRoot/$lowerPath"
