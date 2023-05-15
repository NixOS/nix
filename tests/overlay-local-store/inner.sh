#!/usr/bin/env bash

set -eu -o pipefail

set -x

source common.sh

storeDirs

initLowerStore

mountOverlayfs

### Check status

# Checking for path in lower layer
stat $(toRealPath "$storeA/nix/store" "$path")

# Checking for path in upper layer (should fail)
expect 1 stat $(toRealPath "$storeBTop" "$path")

# Checking for path in overlay store matching lower layer
diff $(toRealPath "$storeA/nix/store" "$path") $(toRealPath "$TEST_ROOT/merged-store/nix/store" "$path")

# Checking requisites query agreement
[[ \
  $(nix-store --store $storeA --query --requisites $drvPath) \
  == \
  $(nix-store --store $storeB --query --requisites $drvPath) \
  ]]

# Checking referrers query agreement
busyboxStore=$(nix store --store $storeA add-path $busybox)
[[ \
  $(nix-store --store $storeA --query --referrers $busyboxStore) \
  == \
  $(nix-store --store $storeB --query --referrers $busyboxStore) \
  ]]

# Checking derivers query agreement
[[ \
  $(nix-store --store $storeA --query --deriver $path) \
  == \
  $(nix-store --store $storeB --query --deriver $path) \
  ]]

# Checking outputs query agreement
[[ \
  $(nix-store --store $storeA --query --outputs $drvPath) \
  == \
  $(nix-store --store $storeB --query --outputs $drvPath) \
  ]]

# Verifying path in lower layer
nix-store --verify-path --store "$storeA" "$path"

# Verifying path in merged-store
nix-store --verify-path --store "$storeB" "$path"

hashPart=$(echo $path | sed "s^$NIX_STORE_DIR/^^" | sed 's/-.*//')

# Lower store can find from hash part
[[ $(nix store --store $storeA path-from-hash-part $hashPart) == $path ]]

# merged store can find from hash part
[[ $(nix store --store $storeB path-from-hash-part $hashPart) == $path ]]

### Do a redundant add

# upper layer should not have it
expect 1 stat $(toRealPath "$storeBTop/nix/store" "$path")

path=$(nix-store --store "$storeB" --add ../dummy)

# lower store should have it from before
stat $(toRealPath "$storeA/nix/store" "$path")

# upper layer should still not have it (no redundant copy)
expect 1 stat $(toRealPath "$storeB/nix/store" "$path")

### Do a build in overlay store

path=$(nix-build ../hermetic.nix --arg busybox $busybox --arg seed 2 --store "$storeB" --no-out-link)

# Checking for path in lower layer (should fail)
expect 1 stat $(toRealPath "$storeA/nix/store" "$path")

# Checking for path in upper layer
stat $(toRealPath "$storeBTop" "$path")

# Verifying path in overlay store
nix-store --verify-path --store "$storeB" "$path"
