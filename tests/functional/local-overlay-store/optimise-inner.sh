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

# Create a file to add to store
dupFilePath="$TEST_ROOT/dup-file"
echo Duplicate > "$dupFilePath"

# Add it to the overlay store (it will be written to the upper layer)
dupFileStorePath=$(nix-store --store "$storeB" --add "$dupFilePath")

# Now add it to the lower store so the store path is duplicated
nix-store --store "$storeA" --add "$dupFilePath"

# Ensure overlayfs and layers and synchronised
remountOverlayfs

dupFilename="${dupFileStorePath#/nix/store}"
lowerPath="$storeA/$dupFileStorePath"
upperPath="$storeBTop/$dupFilename"
overlayPath="$storeBRoot/nix/store/$dupFilename"

# Check store path exists in both layers and overlay
lowerInode=$(stat -c %i "$lowerPath")
upperInode=$(stat -c %i "$upperPath")
overlayInode=$(stat -c %i "$overlayPath")
[[ $upperInode == "$overlayInode" ]]
[[ $upperInode != "$lowerInode" ]]

# Run optimise to deduplicate store paths
nix-store --store "$storeB" --optimise
remountOverlayfs

# Check path only exists in lower store
stat "$lowerPath"
stat "$overlayPath"
expect 1 stat "$upperPath"
