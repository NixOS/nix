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

# --- Test 3: dedup via precomputed hashes ---
# Two store paths each containing a file with identical content.
# With auto-optimise-store enabled, the files should be hardlinked
# (same inode) after copy, proving that the inline per-file NAR hashes
# computed during restorePath match what optimisePath would compute.
dstStore=$(freshDst dedup)
outA=$(nix-build copy-parallel.nix -A dedup-a --no-out-link)
outB=$(nix-build copy-parallel.nix -A dedup-b --no-out-link)
nix copy --to "$dstStore" --option auto-optimise-store true --no-check-sigs "$outA" "$outB"
nix-store --store "$dstStore" --check-validity "$outA"
nix-store --store "$dstStore" --check-validity "$outB"

# The dest store uses /nix/store as its store dir regardless of
# NIX_STORE_DIR, since --to creates a fresh local store with defaults.
dstStoreDir="$dstStore/nix/store"
fileA="$dstStoreDir/$(basename "$outA")/samefile"
fileB="$dstStoreDir/$(basename "$outB")/samefile"

# Both files must exist
[[ -f "$fileA" ]] || fail "file A not found: $fileA"
[[ -f "$fileB" ]] || fail "file B not found: $fileB"

# They should be hardlinked (same inode) due to optimisePath dedup
inodeA=$(stat -c %i "$fileA")
inodeB=$(stat -c %i "$fileB")
[[ "$inodeA" -eq "$inodeB" ]] || fail "expected same inode (dedup), got $inodeA vs $inodeB"
