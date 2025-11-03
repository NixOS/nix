#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# shellcheck disable=SC2016
outPath1=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)
# shellcheck disable=SC2016
outPath2=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)

TODO_NixOS # ignoring the client-specified setting 'auto-optimise-store', because it is a restricted setting and you are not a trusted user
  # TODO: only continue when trusted user or root

inode1="$(stat --format=%i "$outPath1"/foo)"
inode2="$(stat --format=%i "$outPath2"/foo)"
if [ "$inode1" != "$inode2" ]; then
    echo "inodes do not match"
    exit 1
fi

nlink="$(stat --format=%h "$outPath1"/foo)"
if [ "$nlink" != 3 ]; then
    echo "link count incorrect"
    exit 1
fi

# shellcheck disable=SC2016
outPath3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" = "$inode3" ]; then
    echo "inodes match unexpectedly"
    exit 1
fi

# XXX: This should work through the daemon too
NIX_REMOTE="" nix-store --optimise

inode1="$(stat --format=%i "$outPath1"/foo)"
inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" != "$inode3" ]; then
    echo "inodes do not match"
    exit 1
fi

nix-store --gc

if [ -n "$(ls "$NIX_STORE_DIR"/.links)" ]; then
    echo ".links directory not empty after GC"
    exit 1
fi
