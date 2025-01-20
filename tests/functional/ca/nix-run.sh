#!/usr/bin/env bash

source common.sh

flakeDir="$TEST_HOME/flake"
mkdir -p "${flakeDir}"
cp flake.nix "${_NIX_TEST_BUILD_DIR}/ca/config.nix" content-addressed.nix "${flakeDir}"

nix run --no-write-lock-file "path:${flakeDir}#runnable"
