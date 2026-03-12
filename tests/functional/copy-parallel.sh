#!/usr/bin/env bash

source common.sh

TODO_NixOS

needLocalStore "tests LocalStore::addMultipleToStore override"

clearStore

# Build an 8-deep dependency chain in the source store. This is the
# pathological case that previously serialized on topological ordering.
outPath=$(nix-build copy-parallel.nix --no-out-link)

# Destination: fresh chroot store (local store, exercises the override)
dstStore="$TEST_ROOT/dest-store"
rm -rf "$dstStore"

nix copy --to "$dstStore" --no-check-sigs "$outPath"

# Closure invariant: top path must be valid in the destination.
nix-store --store "$dstStore" --check-validity "$outPath"

# Full closure is present: all 8 chain paths registered.
refs=$(nix-store --store "$dstStore" -qR "$outPath")
count=$(echo "$refs" | grep -c chain-)
[[ "$count" -eq 8 ]] || fail "expected 8 chain paths in closure, got $count"
