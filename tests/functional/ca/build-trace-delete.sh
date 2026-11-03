#!/usr/bin/env bash

source common.sh

set -x

needLocalStore "command 'nix store build-trace delete' can’t be used with the daemon"

clearStore

singleOutput=$(nix-instantiate ./nondeterministic-ns.nix -A singleOut)
multiOutput=$(nix-instantiate ./nondeterministic-ns.nix -A multiOut)

# First build
singleOutPath=$(nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link)
nix-store --delete "$singleOutPath"
# The build trace outlives the output
nix store build-trace info "$singleOutput"^out
nix store build-trace delete "$singleOutput"^out
expect 1 nix store build-trace info "$singleOutput"^out
nix-build ./nondeterministic-ns.nix -A singleOut --no-out-link

# Multi-output first
nix-build ./nondeterministic-ns.nix -A multiOut --no-out-link
multiOutPath=$(nix store build-trace info "$multiOutput"^out --json | jq -r '.[] | .opaquePath | select(.)')
multiLibPath=$(nix store build-trace info "$multiOutput"^lib --json | jq -r '.[] | .opaquePath | select(.)')

# We should be able to delete multiple build traces/realisations at once
nix-store --delete "$multiOutPath" "$multiLibPath"
nix store build-trace delete "$multiOutput"^out "$multiOutput"^lib

# out and lib should be deleted, but dev should not
expect 1 nix store build-trace info "$multiOutput"^out
expect 1 nix store build-trace info "$multiOutput"^lib
nix store build-trace info "$multiOutput"^dev
