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
    testCollectGarbageCheckGenerations "1 2 3 4 5 6" "$@"
}

testCollectGarbageMockTimestamps () {
    # Modifies the date the generations were activated
    profiles_prefix=${1:-"${profiles}/profile"}
    touch -h -m -d "  -4 days           -1 seconds" "${profiles_prefix}-1-link"
    touch -h -m -d "  -3 days           -1 seconds" "${profiles_prefix}-2-link"
    touch -h -m -d "  -2 days -12 hours -1 seconds" "${profiles_prefix}-3-link"
    touch -h -m -d "  -2 days           -1 seconds" "${profiles_prefix}-4-link"
    touch -h -m -d "  -1 days -16 hours -1 seconds" "${profiles_prefix}-5-link"
    touch -h -m -d "  -1 days  -8 hours -1 seconds" "${profiles_prefix}-6-link"
}

testCollectGarbageCheckGenerations () {
    # expected should be a space separated list of generation ids
    expected="$1"
    actual=$(nix-env --list-generations "${@:2}" | awk '{{print $1}}' | tr '\n' ' ')
    [[ "${actual%" "}" == "${expected}" ]]
}

# Basic test that should leave 1 generation
testCollectGarbageSetup
nix-collect-garbage -d
testCollectGarbageCheckGenerations "6"

# Run the same test, but forcing the profiles an arbitrary location.
rm ~/.nix-profile
ln -s $TEST_ROOT/blah ~/.nix-profile
testCollectGarbageSetup
nix-collect-garbage -d
testCollectGarbageCheckGenerations "6"

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
testCollectGarbageCheckGenerations "6" --profile "$NIX_STATE_DIR/profiles/per-user/me"

# Delete all except latest 4 generations
testCollectGarbageSetup
nix-collect-garbage --delete-older-than "+4"
testCollectGarbageCheckGenerations "3 4 5 6"

# Delete everything older than 5 days
# All profiles are younger than 5 days
testCollectGarbageSetup
testCollectGarbageMockTimestamps
nix-collect-garbage --delete-older-than "5d"
testCollectGarbageCheckGenerations "1 2 3 4 5 6"

# Delete everything older than 4 days
# Profile 1 was active at 4 days ago so it will be kept
testCollectGarbageSetup
testCollectGarbageMockTimestamps
nix-collect-garbage --delete-older-than "4d"
testCollectGarbageCheckGenerations "1 2 3 4 5 6"

# Delete everything older than 3 days
# Profile 2 was active at 3 days ago so it will be kept
testCollectGarbageSetup
testCollectGarbageMockTimestamps
nix-collect-garbage --delete-older-than "3d"
testCollectGarbageCheckGenerations "2 3 4 5 6"

# Delete everything older than 2 days
# Profile 4 was active at 2 days ago so it will be kept
testCollectGarbageSetup
testCollectGarbageMockTimestamps
nix-collect-garbage --delete-older-than "2d"
testCollectGarbageCheckGenerations "4 5 6"

# Delete everything older than 1 day
# Profile 6 is current and was active at 1 day ago so it will be kept
testCollectGarbageSetup
testCollectGarbageMockTimestamps
nix-collect-garbage --delete-older-than "1d"
testCollectGarbageCheckGenerations "6"
