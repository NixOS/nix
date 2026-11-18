#!/usr/bin/env bash

# Rebuilding a floating CA output must see the same $out as the first build:
# content may depend on it beyond rewritable self references (build ids).

source common.sh

requireDaemonNewerThan "2.36.0pre20260916"

out1=$(nix-build ./scratch-path.nix --no-out-link)
sum1=$(cat "$out1/sum")

nix-store --delete "$out1"
out2=$(nix-build ./scratch-path.nix --no-out-link)

[[ "$out1" == "$out2" ]]
[[ "$sum1" == "$(cat "$out2/sum")" ]]

nix-build ./scratch-path.nix --no-out-link --check
