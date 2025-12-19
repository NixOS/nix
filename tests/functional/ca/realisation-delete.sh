#!/usr/bin/env bash

source common.sh

set -x

needLocalStore "command 'nix realisation delete' can’t be used with the daemon"

clearStore

singleOutput=$(nix-instantiate ./nondeterministic-ns.nix -A singleOut)
multiOutput=$(nix-instantiate ./nondeterministic-ns.nix -A multiOut)

# First build
singleOutPath=$(nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link)
nix-store --delete "$singleOutPath"
# We should still have the build trace/realisation in the database, so second build will fail
expect 1 nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link
# Deleting the build trace/realisation should fix it though
nix realisation delete "$singleOutput"^out
nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link

# Multi-output first
nix-build ./nondeterministic-ns.nix -A multiOut --no-out-link
multiOutPath=$(nix realisation info "$multiOutput"^out --json | jq -r '.[] | .opaquePath | select(.)')
multiLibPath=$(nix realisation info "$multiOutput"^out --json | jq -r '.[] | .opaquePath | select(.)')

# We should be able to delete multiple build traces/realisations at once
nix-store --delete "$multiOutPath" "$multiLibPath"
nix realisation delete "$multiOutput"^out,lib

# out and lib should be deleted, but dev should not
expect 1 nix realisation info "$multiOutput"^out
expect 1 nix realisation info "$multiOutput"^lib
nix realisation info "$multiOutput"^dev
