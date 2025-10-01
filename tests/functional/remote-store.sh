#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

# Ensure "fake ssh" remote store works just as legacy fake ssh would.
nix --store ssh-ng://localhost?remote-store="$TEST_ROOT"/other-store doctor

# Ensure that store info trusted works with ssh-ng://
nix --store ssh-ng://localhost?remote-store="$TEST_ROOT"/other-store store info --json | jq -e '.trusted'

startDaemon

if isDaemonNewer "2.15pre0"; then
    # Ensure that ping works trusted with new daemon
    nix store info --json | jq -e '.trusted'
    # Suppress grumpiness about multiple nixes on PATH
    (nix doctor || true) 2>&1 | grep 'You are trusted by'
else
    # And the the field is absent with the old daemon
    nix store info --json | jq -e 'has("trusted") | not'
fi

# Test import-from-derivation through the daemon.
[[ $(nix eval --impure --raw --file ./ifd.nix) = hi ]]

NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs-test-case.sh

nix-store --gc --max-freed 1K

nix-store --dump-db > "$TEST_ROOT"/d1
NIX_REMOTE='' nix-store --dump-db > "$TEST_ROOT"/d2
cmp "$TEST_ROOT"/d1 "$TEST_ROOT"/d2

killDaemon
