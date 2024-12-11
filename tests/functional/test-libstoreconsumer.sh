#!/usr/bin/env bash

source common.sh

drv="$(nix-instantiate simple.nix)"
cat "$drv"
out="$("${_NIX_TEST_BUILD_DIR}/test-libstoreconsumer/test-libstoreconsumer" "$drv")"
grep -F "Hello World!" < "$out/hello"
