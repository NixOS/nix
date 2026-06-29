#!/usr/bin/env bash

source common.sh

clearStore

out=$(nix-build ./varlink.nix -A dynamicWrapper --no-out-link)

test "$(cat "$out"/bar)" == "foo"
