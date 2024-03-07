#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

## Test `nix-collect-garbage -d`

# TODO make `nix-env` doesn't work with CA derivations, and make
# `ca/nix-collect-garbage-d.sh` wrapper.


testCollectGarbageSetup () {
    clearProfiles
    # Run six `nix-env` commands, should create six generations of
    # the profile
    nix-env -f ./user-envs.nix -i foo-1.0 "$@"
    nix-env -f ./user-envs.nix -i foo-2.0pre1 "$@"
    nix-env -f ./user-envs.nix -i bar-0.1 "$@"
    nix-env -f ./user-envs.nix -i foo-2.0 "$@"
    nix-env -f ./user-envs.nix -i bar-0.1.1 "$@"
    nix-env -f ./user-envs.nix -i foo-0.1 "$@"
    [[ $(nix-env --list-generations "$@" | wc -l) -eq 6 ]]
}

# Basic test that should leave 1 generation
testCollectGarbageSetup
nix-collect-garbage -d
[[ $(nix-env --list-generations | wc -l) -eq 1 ]]

# Run the same test, but forcing the profiles an arbitrary location.
rm ~/.nix-profile
ln -s $TEST_ROOT/blah ~/.nix-profile
testCollectGarbageSetup
nix-collect-garbage -d
[[ $(nix-env --list-generations | wc -l) -eq 1 ]]

# Run the same test, but forcing the profiles at their legacy location under
# /nix/var/nix.
#
# Note that we *don't* use the default profile; `nix-collect-garbage` will
# need to check the legacy conditional unconditionally not just follow
# `~/.nix-profile` to pass this test.
#
# Regression test for #8294
rm ~/.nix-profile
testCollectGarbageSetup --profile "$NIX_STATE_DIR/profiles/per-user/me"
nix-collect-garbage -d
[[ $(nix-env --list-generations --profile "$NIX_STATE_DIR/profiles/per-user/me" | wc -l) -eq 1 ]]
