#!/usr/bin/env bash

set -eu -o pipefail

set -x

source common.sh

export NIX_CONFIG='build-users-group = '

# Creating testing directories

storeA="$TEST_ROOT/store_a"
storeB="local-overlay?root=$TEST_ROOT/store_b&lower-store=$TEST_ROOT/merged-store"
storeBTop="$TEST_ROOT/store_b"

mkdir -p "$TEST_ROOT"/{store_a,store_b,merged-store,workdir}

# Mounting Overlay Store

## Restore normal, because we are using these chroot stores
#NIX_STORE_DIR=/nix/store

nix-store --store "$TEST_ROOT/store_a" --add dummy
nix-store --store "$TEST_ROOT/store_b" --add dummy

mount -t overlay overlay \
  -o lowerdir="$TEST_ROOT/store_a/nix/store" \
  -o upperdir="$TEST_ROOT/store_b/nix/store" \
  -o workdir="$TEST_ROOT/workdir" \
  "$TEST_ROOT/merged-store" || skipTest "overlayfs is not supported"

# Add in lower
NIX_REMOTE=$storeA source add.sh

# Add in layered
NIX_REMOTE=$storeB source add.sh

#busyboxExpr="\"\${$(dirname "$busybox")}/$(basename "$busybox")\""
path_a=$(nix-build ./hermetic.nix --arg busybox "$busybox" --store "$storeA")

# Checking for Path in store_a
stat "$TEST_ROOT/store_a/$path_a"

# Checking for Path in store_b
expect 1 stat "$TEST_ROOT/store_b/$path_a"

# Checking for Path in merged-store
ls "$TEST_ROOT/merged-store/$(echo "$path_a" | sed 's|/nix/store/||g')"


# Verifying path in store_a
nix-store --verify-path --store "$storeA" "$path_a"

# Verifiying path in merged-store (Should fail)
expect 1 nix-store --verify-path --store "$storeB" "$path_a"

# Verifying path in store_b (Should fail)
expect 1 nix-store --verify-path --store "$storeBTop" "$path_a"

path_b=$(nix-build ./hermetic.nix --arg busybox $busybox --store "$storeB")
