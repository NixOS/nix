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

# Realise a derivation from the lower store to propagate paths to overlay DB
nix-store --store "$storeB" --realise $drvPath

# Also ensure dummy file exists in overlay DB
dummyPath=$(nix-store --store "$storeB" --add ../dummy)

# Verify should be successful at this point
nix-store --store "$storeB" --verify --check-contents

# Now delete one of the derivation inputs in the lower store
inputDrvFullPath=$(find "$storeA" -name "*-hermetic-input-1.drv")
inputDrvPath=${inputDrvFullPath/*\/nix\/store\///nix/store/}
rm -v "$inputDrvFullPath"

# And truncate the contents of dummy file in lower store
find "$storeA" -name "*-dummy" -exec truncate -s 0 {} \;

# Verify should fail with the messages about missing input and modified dummy file
verifyOutput=$(expectStderr 1 nix-store --store "$storeB" --verify --check-contents --repair)
<<<"$verifyOutput" grepQuiet "path '$inputDrvPath' disappeared, but it still has valid referrers!"
<<<"$verifyOutput" grepQuiet "path '$dummyPath' was modified! expected hash"
<<<"$verifyOutput" grepQuiet "store does not support --verify --repair"
