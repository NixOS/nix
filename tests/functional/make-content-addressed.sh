#!/usr/bin/env bash

source common.sh

enableFeatures "ca-derivations"

clearStore

# Build a floating output with a self-reference.
outPath1=$(nix build --print-out-paths --no-link --impure --file ./make-content-addressed.nix --arg zeroSelfReference false)

# Make it content-addressed.
outPathCA=$(nix store make-content-addressed --json "$outPath1" | jq -r '.rewrites | map(.) | .[]')
[[ $outPath1 != "$outPathCA" ]]

# Check that the result is content-addressed.
nix path-info --json --json-format 2 "$outPathCA" | jq -e 'all(.info[].ca.method; . == "nar")'

# Verify the CA-rewritten path.
nix store verify "$outPathCA"

# Building the same derivation but as a CA derivation should produce the same store path.
outPathCA2=$(NIX_TESTS_CA_BY_DEFAULT=1 nix build --print-out-paths --no-link --impure --file ./make-content-addressed.nix --arg zeroSelfReference false)
[[ $outPathCA == "$outPathCA2" ]]

# Build the same derivation except that one self-reference is zeroed out.
outPathZero=$(nix build --print-out-paths --no-link --impure --file ./make-content-addressed.nix --arg zeroSelfReference true)

# Make it content-addressed as well.
outPathZeroCA=$(nix store make-content-addressed --json "$outPathZero" | jq -r '.rewrites | map(.) | .[]')

# The resulting paths should be different, since HashModuloSink hashes the positions of self-references.
# https://github.com/NixOS/nix/issues/15837
[[ $outPathCA != "$outPathZeroCA" ]]

outPathZeroCA2=$(NIX_TESTS_CA_BY_DEFAULT=1 nix build --print-out-paths --no-link --impure --file ./make-content-addressed.nix --arg zeroSelfReference true)
[[ $outPathZeroCA == "$outPathZeroCA2" ]]
