#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.12.0"
clearStoreIfPossible
if isTestOnNixOS; then
    skipTest "requires test-managed daemon"
fi
startDaemon

drv_a=$(nix-instantiate ./serve-max-silent-time.nix -A silentA)
drv_b=$(nix-instantiate ./serve-max-silent-time.nix -A silentB)

"${_NIX_TEST_BUILD_DIR}/test-serve-max-silent-time/test-serve-max-silent-time" \
  "$drv_a" 1 expect-timeout \
  "$drv_b" 8 expect-success
