#!/usr/bin/env bash

source common.sh

sed -e "s|@localstatedir@|$TEST_ROOT/profile-var|g" -e "s|@coreutils@|$coreutils|g" < ../../scripts/nix-profile.sh.in > "$TEST_ROOT"/nix-profile.sh

rm -rf "$TEST_HOME" "$TEST_ROOT/profile-var"
mkdir -p "$TEST_HOME"
USER=foobar $SHELL -e -c ". $TEST_ROOT/nix-profile.sh; set"
USER=foobar $SHELL -e -c ". $TEST_ROOT/nix-profile.sh" # test idempotency
