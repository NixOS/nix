#!/usr/bin/env bash

source common.sh

clearStore

out=$(nix-build ./varlink.nix -A trivial --no-out-link)

test "$(cat "$out"/foo)" == "bar"
