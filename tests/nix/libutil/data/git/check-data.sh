#!/usr/bin/env bash

set -eu -o pipefail

export TEST_ROOT=$(realpath ${TMPDIR:-/tmp}/nix-test)/git-hashing/check-data
mkdir -p $TEST_ROOT

repo="$TEST_ROOT/scratch"
git init "$repo"

git -C "$repo" config user.email "you@example.com"
git -C "$repo" config user.name "Your Name"

# `-w` to write for tree test
freshlyAddedHash=$(git -C "$repo" hash-object -w -t blob --stdin < "./hello-world.bin")
encodingHash=$(sha1sum -b < "./hello-world-blob.bin" | head -c 40)

# If the hashes match, then `hello-world-blob.bin` must be the encoding
# of `hello-world.bin`.
[[ "$encodingHash" == "$freshlyAddedHash" ]]

# Create empty directory object for tree test
echo -n | git -C "$repo" hash-object -w -t tree --stdin

# Relies on both child hashes already existing in the git store
freshlyAddedHash=$(git -C "$repo" mktree < "./tree.txt")
encodingHash=$(sha1sum -b < "./tree.bin" | head -c 40)

# If the hashes match, then `tree.bin` must be the encoding of the
# directory denoted by `tree.txt` interpreted as git directory listing.
[[ "$encodingHash" == "$freshlyAddedHash" ]]
