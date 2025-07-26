#!/usr/bin/env bash

set -eu -o pipefail

TEST_ROOT=$(realpath "${TMPDIR:-/tmp}/nix-test")/git-hashing/check-data
export TEST_ROOT
mkdir -p "$TEST_ROOT"

for hash in sha1 sha256; do
  repo="$TEST_ROOT/scratch-$hash"
  git init "$repo" --object-format="$hash"

  git -C "$repo" config user.email "you@example.com"
  git -C "$repo" config user.name "Your Name"

  # `-w` to write for tree test
  freshlyAddedHash=$(git -C "$repo" hash-object -w -t blob --stdin < "./hello-world.bin")
  encodingHash=$("${hash}sum" -b < "./hello-world-blob.bin" | sed 's/ .*//')

  # If the hashes match, then `hello-world-blob.bin` must be the encoding
  # of `hello-world.bin`.
  [[ "$encodingHash" == "$freshlyAddedHash" ]]

  # Create empty directory object for tree test
  echo -n | git -C "$repo" hash-object -w -t tree --stdin

  # Relies on both child hashes already existing in the git store
  tree=tree-${hash}
  freshlyAddedHash=$(git -C "$repo" mktree < "${tree}.txt")
  encodingHash=$("${hash}sum" -b < "${tree}.bin" | sed 's/ .*//')

  # If the hashes match, then `tree.bin` must be the encoding of the
  # directory denoted by `tree.txt` interpreted as git directory listing.
  [[ "$encodingHash" == "$freshlyAddedHash" ]]
done
