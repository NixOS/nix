#!/usr/bin/env bash

source common.sh

TODO_NixOS

needLocalStore "tests LocalStore::addMultipleToStore override"

clearStore

freshDst() {
    local d="$TEST_ROOT/dest-store-$1"
    chmod -R u+w "$d" 2>/dev/null || true
    rm -rf "$d"
    echo "$d"
}

# --- Test 1: deep chain, single chunk ---
# 8-deep dependency chain. Previously serialized on topological ordering.
dstStore=$(freshDst chain)
outPath=$(nix-build copy-parallel.nix -A chain --no-out-link)
nix copy --to "$dstStore" --no-check-sigs "$outPath"
nix-store --store "$dstStore" --check-validity "$outPath"
count=$(nix-store --store "$dstStore" -qR "$outPath" | grep -c chain-)
[[ "$count" -eq 8 ]] || fail "expected 8 chain paths, got $count"

# --- Test 2: wide closure, forced chunking ---
# 21 paths / chunk size 8 = 3 chunks. wide-top references leaves spread
# across all chunks, exercising cross-chunk dependency resolution.
dstStore=$(freshDst wide)
outPath=$(nix-build copy-parallel.nix -A wide --no-out-link)
_NIX_TEST_CHUNK_SIZE=8 nix copy --to "$dstStore" --no-check-sigs "$outPath"
nix-store --store "$dstStore" --check-validity "$outPath"
count=$(nix-store --store "$dstStore" -qR "$outPath" | grep -c wide-)
[[ "$count" -eq 21 ]] || fail "expected 21 wide paths (20 leaves + top), got $count"
