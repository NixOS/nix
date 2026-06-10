#!/usr/bin/env bash

source common.sh

: "${config_nix?}"

flakeDir="$TEST_HOME/flake"
mkdir -p "${flakeDir}"
cp flake.nix "$config_nix" content-addressed.nix "${flakeDir}"

nix run --no-write-lock-file "path:${flakeDir}#runnable"
