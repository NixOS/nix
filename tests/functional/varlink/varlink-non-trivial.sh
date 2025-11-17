#!/usr/bin/env bash

source common.sh

clearStore

out=$(nix-build ./varlink.nix -A nonTrivialOuter --no-out-link)

test "$(cat "$out/result")" == "word env var e is hello, from e!"
