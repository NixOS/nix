source ../common.sh

export LIBMOUNT_FORCE_MOUNT2=always

requireEnvironment () {
  requireSandboxSupport
  [[ $busybox =~ busybox ]] || skipTest "no busybox"
  if [[ $(uname) != Linux ]]; then skipTest "Need Linux for overlayfs"; fi
  needLocalStore "The test uses --store always so we would just be bypassing the daemon"
}

addConfig () {
    echo "$1" >> "$NIX_CONF_DIR/nix.conf"
}

setupConfig () {
  addConfig "require-drop-supplementary-groups = false"
  addConfig "build-users-group = "
}

enableFeatures "local-overlay-store"

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
    umount "$storeBRoot/nix/store"
    rm -r $storeVolume/workdir
  }
  trap cleanupOverlay EXIT
}

remountOverlayfs () {
  mount -o remount "$storeBRoot/nix/store"
}

toRealPath () {
  storeDir=$1; shift
  storePath=$1; shift
  echo $storeDir$(echo $storePath | sed "s^${NIX_STORE_DIR:-/nix/store}^^")
}

initLowerStore () {
  # Init lower store with some stuff
  nix-store --store "$storeA" --add ../dummy

  # Build something in lower store
  drvPath=$(nix-instantiate --store $storeA ../hermetic.nix --arg withFinalRefs true --arg busybox "$busybox" --arg seed 1)
  pathInLowerStore=$(nix-store --store "$storeA" --realise $drvPath)
}

execUnshare () {
  exec unshare --mount --map-root-user "$SHELL" "$@"
}

addTextToStore() {
  storeDir=$1; shift
  filename=$1; shift
  content=$1; shift
  filePath="$TEST_HOME/$filename"
  echo "$content" > "$filePath"
  nix-store --store "$storeDir" --add "$filePath"
}
