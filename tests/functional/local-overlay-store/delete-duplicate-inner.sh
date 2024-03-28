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

# Add to overlay before lower to ensure file is duplicated
upperPath=$(nix-store --store "$storeB" --add delete-duplicate.sh)
lowerPath=$(nix-store --store "$storeA" --add delete-duplicate.sh)
[[ "$upperPath" = "$lowerPath" ]]

# Check there really are two files with different inodes
upperInode=$(stat -c %i "$storeBRoot/$upperPath")
lowerInode=$(stat -c %i "$storeA/$lowerPath")
[[ "$upperInode" != "$lowerInode" ]]

# Now delete file via the overlay store
nix-store --store "$storeB&remount-hook=$PWD/remount.sh" --delete "$upperPath"

# Check there is no longer a file in upper layer
expect 1 stat "$storeBTop/${upperPath##/nix/store/}"

# Check that overlay file is now the one in lower layer
upperInode=$(stat -c %i "$storeBRoot/$upperPath")
lowerInode=$(stat -c %i "$storeA/$lowerPath")
[[ "$upperInode" = "$lowerInode" ]]
