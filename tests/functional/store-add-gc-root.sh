#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

function delete() { nix-store --delete "$storePath"; }
function build() { nix build --no-link --print-out-paths -f simple.nix; }

# Check that path can be deleted
storePath=$(build)
delete

# Check that add-gc-root prevents deletion,
# and removing the root make it deletable again.
storePath=$(build)
ln -sn "$storePath" myroot
nix store add-gc-root myroot
if delete; then false; fi
rm myroot
delete

# Create several roots at once
storePath=$(build)
ln -sn "$storePath" myroot1
ln -sn "$storePath" myroot2
ln -sn "$storePath" myroot3
nix store add-gc-root myroot1 myroot2 myroot3
if delete; then false; fi
rm myroot3 myroot2
if delete; then false; fi
rm myroot1
delete

# Test detection of invalid roots
# 1. path deleted before root creation
storePath=$(build)
delete
ln -sn "$storePath" myroot
if nix store add-gc-root myroot; then false; fi
nix store add-gc-root --no-check myroot
rm myroot

# 2. invalid path
ln -sn /invalid-target myroot
if nix store add-gc-root myroot; then false; fi
nix store add-gc-root --no-check myroot
rm myroot

# Fail when trying to setup a direct root
storePath=$(build)
if nix store add-gc-root "$storePath"; then false; fi

