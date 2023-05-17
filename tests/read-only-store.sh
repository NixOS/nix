source common.sh

clearStore

## Testing read-only mode without forcing the underlying store to actually be read-only

# Make sure the command fails when the store doesn't already have a database
expectStderr 1 nix-store --store local?read-only=true --add dummy | grepQuiet "unable to create database while in read-only mode"

# Make sure the store actually has a current-database
nix-store --add dummy

# Try again and make sure we fail when adding a item not already in the store
expectStderr 1 nix-store --store local?read-only=true --add eval.nix | grepQuiet "attempt to write a readonly database"

# Make sure we can get an already-present store-path in the database
nix-store --store local?read-only=true --add dummy

## Testing read-only mode with an underlying store that is actually read-only

# Ensure store is actually read-only
chmod -R -w $TEST_ROOT/store
chmod -R -w $TEST_ROOT/var

# Make sure we fail on add operations on the read-only store
# This is only for adding files that are not *already* in the store
expectStderr 1 nix-store --add eval.nix | grepQuiet "error: opening lock file '$(readlink -e $TEST_ROOT)/var/nix/db/big-lock'"
expectStderr 1 nix-store --store local?read-only=true --add eval.nix | grepQuiet "Permission denied"

# Should succeed
nix-store --store local?read-only=true --add dummy
