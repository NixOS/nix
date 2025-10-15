#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

## Test `nix-collect-garbage -d`

# TODO make `nix-env` doesn't work with CA derivations, and make
# `ca/nix-collect-garbage-d.sh` wrapper.

testCollectGarbageD () {
    clearProfiles
    # Run two `nix-env` commands, should create two generations of
    # the profile
    nix-env -f ./user-envs.nix -i foo-1.0 "$@"
    nix-env -f ./user-envs.nix -i foo-2.0pre1 "$@"
    [[ $(nix-env --list-generations "$@" | wc -l) -eq 2 ]]

    # Clear the profile history. There should be only one generation
    # left
    nix-collect-garbage -d
    [[ $(nix-env --list-generations "$@" | wc -l) -eq 1 ]]
}

testCollectGarbageD

# Run the same test, but forcing the profiles an arbitrary location.
rm ~/.nix-profile
ln -s "$TEST_ROOT"/blah ~/.nix-profile
testCollectGarbageD

# Run the same test, but forcing the profiles at their legacy location under
# /nix/var/nix.
#
# Note that we *don't* use the default profile; `nix-collect-garbage` will
# need to check the legacy conditional unconditionally not just follow
# `~/.nix-profile` to pass this test.
#
# Regression test for #8294
rm ~/.nix-profile
testCollectGarbageD --profile "$NIX_STATE_DIR/profiles/per-user/me"
