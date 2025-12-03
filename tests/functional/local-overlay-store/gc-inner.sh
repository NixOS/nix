#!/usr/bin/env bash

set -eu -o pipefail

source common.sh

# Avoid store dir being inside sandbox build-dir
unset NIX_STORE_DIR
unset NIX_STATE_DIR

setupStoreDirs

initLowerStore

mountOverlayfs

export NIX_REMOTE="$storeB"
stateB="$storeBRoot/nix/var/nix"
outPath=$(nix-build ../hermetic.nix --no-out-link --arg busybox "$busybox" --arg seed 2)

# Set a GC root.
mkdir -p "$stateB"
rm -f "$stateB/gcroots/foo"
ln -sf "$outPath" "$stateB/gcroots/foo"

[ "$(nix-store -q --roots "$outPath")" = "$stateB/gcroots/foo -> $outPath" ]

nix-store --gc --print-roots | grep "$outPath"
nix-store --gc --print-live | grep "$outPath"
if nix-store --gc --print-dead | grep -E "$outPath"$; then false; fi

nix-store --gc --print-dead

expect 1 nix-store --delete "$outPath"
test -e "$storeBRoot/$outPath"

shopt -s nullglob
for i in "$storeBRoot"/*; do
    if [[ $i =~ /trash ]]; then continue; fi # compat with old daemon
    touch "$i".lock
    touch "$i".chroot
done

nix-collect-garbage

# Check that the root and its dependencies haven't been deleted.
cat "$storeBRoot/$outPath"

rm "$stateB/gcroots/foo"

nix-collect-garbage

# Check that the output has been GC'd.
test ! -e "$outPath"

# Check that the store is empty.
# shellcheck disable=SC2012
[ "$(ls -1 "$storeBTop" | wc -l)" = "0" ]
