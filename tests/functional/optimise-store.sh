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
    fail "inodes do not match"
fi

nlink="$(stat --format=%h "$outPath1"/foo)"
if [ "$nlink" != 3 ]; then
    fail "link count incorrect"
fi

# shellcheck disable=SC2016
outPath3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" = "$inode3" ]; then
    fail "inodes match unexpectedly"
fi

# XXX: This should work through the daemon too
NIX_REMOTE="" nix-store --optimise

inode1="$(stat --format=%i "$outPath1"/foo)"
inode3="$(stat --format=%i "$outPath3"/foo)"
if [ "$inode1" != "$inode3" ]; then
    fail "inodes do not match"
fi

# shellcheck disable=SC2016
outPath4=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo4"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

NIX_REMOTE="" nix store optimise

inode1="$(stat --format=%i "$outPath1"/foo)"
inode4="$(stat --format=%i "$outPath4"/foo)"
if [ "$inode1" != "$inode4" ]; then
    fail "inodes do not match"
fi

# alias of optimise
if ! NIX_REMOTE="" nix store optimize; then
    fail "nix store optimize alias is not present"
fi

nix-store --gc

if [ -n "$(ls "$NIX_STORE_DIR"/.links)" ]; then
    fail ".links directory not empty after GC"
fi
