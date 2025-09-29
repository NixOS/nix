# shellcheck shell=bash
source ../common/vars.sh
source ../common/functions.sh

TODO_NixOS

# The new Linux mount interface does not seem to support remounting
# OverlayFS mount points.
#
# It is not clear whether this is intentional or not:
#
# The  kernel source code [1] would seem to indicate merely remounting
# while *changing* mount options is now an error because it erroneously
# succeeded (by ignoring those new options) before. However, we are
# *not* trying to remount with changed options, and are still hitting
# the failure when using the new interface.
#
# For further details, see these `util-linux` issues:
#
#  - https://github.com/util-linux/util-linux/issues/2528
#  - https://github.com/util-linux/util-linux/issues/2576
#
# In the meantime, setting this environment variable to "always" will
# force the use of the old mount interface, keeping the remounting
# working and these tests passing.
#
# [1]: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/fs/overlayfs/params.c?id=3006adf3be79cde4d14b1800b963b82b6e5572e0#n549
export LIBMOUNT_FORCE_MOUNT2=always

requireEnvironment () {
  requireSandboxSupport
  [[ $busybox =~ busybox ]] || skipTest "no busybox"
  if [[ $(uname) != Linux ]]; then skipTest "Need Linux for overlayfs"; fi
  needLocalStore "The test uses --store always so we would just be bypassing the daemon"
}

addConfig () {
    echo "$1" >> "$test_nix_conf"
}

setupConfig () {
  addConfig "require-drop-supplementary-groups = false"
  addConfig "build-users-group = "
  enableFeatures "local-overlay-store"
}

setupStoreDirs () {
  # Attempt to create store dirs on tmpfs volume.
  # This ensures lowerdir, upperdir and workdir will be on
  # a consistent filesystem that fully supports OverlayFS.
  storeVolume="$TEST_ROOT/stores"
  mkdir -p "$storeVolume"
  mount -t tmpfs tmpfs "$storeVolume" || true  # But continue anyway if that fails.

  storeA="$storeVolume/store-a"
  storeBTop="$storeVolume/store-b"
  storeBRoot="$storeVolume/merged-store"
  # shellcheck disable=SC2034
  storeB="local-overlay://?root=$storeBRoot&lower-store=$storeA&upper-layer=$storeBTop"
  # Creating testing directories
  mkdir -p "$storeVolume"/{store-a/nix/store,store-b,merged-store/nix/store,workdir}
}

# Mounting Overlay Store
mountOverlayfs () {
  mount -t overlay overlay \
    -o lowerdir="$storeA/nix/store" \
    -o upperdir="$storeBTop" \
    -o workdir="$storeVolume/workdir" \
    "$storeBRoot/nix/store" \
    || skipTest "overlayfs is not supported"

  cleanupOverlay () {
    # shellcheck disable=2317
    umount -n "$storeBRoot/nix/store"
    # shellcheck disable=2317
    rm -r "$storeVolume"/workdir
  }
  trap cleanupOverlay EXIT
}

remountOverlayfs () {
  mount -o remount "$storeBRoot/nix/store"
}

toRealPath () {
  storeDir=$1; shift
  storePath=$1; shift
  # shellcheck disable=SC2001
  echo "$storeDir""$(echo "$storePath" | sed "s^${NIX_STORE_DIR:-/nix/store}^^")"
}

initLowerStore () {
  # Init lower store with some stuff
  nix-store --store "$storeA" --add ../dummy

  # Build something in lower store
  drvPath=$(nix-instantiate --store "$storeA" ../hermetic.nix --arg withFinalRefs true --arg busybox "$busybox" --arg seed 1)
  # shellcheck disable=SC2034
  pathInLowerStore=$(nix-store --store "$storeA" --realise "$drvPath")
}

addTextToStore() {
  storeDir=$1; shift
  filename=$1; shift
  content=$1; shift
  filePath="$TEST_HOME/$filename"
  echo "$content" > "$filePath"
  nix-store --store "$storeDir" --add "$filePath"
}
