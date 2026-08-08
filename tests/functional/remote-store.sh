#!/usr/bin/env bash

source common.sh

TODO_NixOS

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

# `nix-env --install` through the daemon should not warn about
# `use-xdg-base-directories`, and should still install successfully.
expectStderr 0 nix-env --option use-xdg-base-directories true \
    -p "$TEST_ROOT/daemon-profile" -f ./user-envs.nix -i foo-1.0 \
    | grepQuietInverse "ignoring the client-specified setting 'use-xdg-base-directories'"
nix-env -p "$TEST_ROOT/daemon-profile" -q '*' | grepQuiet foo-1.0

# Test import-from-derivation through the daemon.
[[ $(nix eval --impure --raw --file ./ifd.nix) = hi ]]

NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs-test-case.sh

nix-store --gc --max-freed 1K

nix-store --dump-db > "$TEST_ROOT"/d1
NIX_REMOTE='' nix-store --dump-db > "$TEST_ROOT"/d2
cmp "$TEST_ROOT"/d1 "$TEST_ROOT"/d2

killDaemon
