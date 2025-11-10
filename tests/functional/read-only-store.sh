#!/usr/bin/env bash

source common.sh

enableFeatures "read-only-local-store"

needLocalStore "cannot open store read-only when daemon has already opened it writeable"

TODO_NixOS

clearStore

happy () {
    # We can do a read-only query just fine with a read-only store
    nix --store local?read-only=true path-info "$dummyPath"

    # `local://` also works.
    nix --store local://?read-only=true path-info "$dummyPath"

    # We can "write" an already-present store-path a read-only store, because no IO is actually required
    nix-store --store local?read-only=true --add dummy
}
## Testing read-only mode without forcing the underlying store to actually be read-only

# Make sure the command fails when the store doesn't already have a database
expectStderr 1 nix-store --store local?read-only=true --add dummy | grepQuiet "database does not exist, and cannot be created in read-only mode"

# Make sure the store actually has a current-database, with at least one store object
dummyPath=$(nix-store --add dummy)

# Try again and make sure we fail when adding a item not already in the store
expectStderr 1 nix-store --store local?read-only=true --add eval.nix | grepQuiet "attempt to write a readonly database"

# Test a few operations that should work with the read-only store in its current state
happy

## Testing read-only mode with an underlying store that is actually read-only

# Ensure store is actually read-only
chmod -R -w "$TEST_ROOT"/store
chmod -R -w "$TEST_ROOT"/var

# Make sure we fail on add operations on the read-only store
# This is only for adding files that are not *already* in the store
# Should show enhanced error message with helpful context
expectStderr 1 nix-store --add eval.nix | grepQuiet "This command may have been run as non-root in a single-user Nix installation"
expectStderr 1 nix-store --store local?read-only=true --add eval.nix | grepQuiet "Permission denied"

# Test the same operations from before should again succeed
happy
