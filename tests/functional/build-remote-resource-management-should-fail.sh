#!/usr/bin/env bash

source common.sh

enableFeatures "resource-management"

requireSandboxSupport
[[ $busybox =~ busybox ]] || skipTest "no busybox"

here=$(readlink -f "$(dirname "${BASH_SOURCE[0]}")")
export NIX_USER_CONF_FILES=$here/config/nix-with-resource-management.conf

expectStderr 1 nix build -Lvf resource-management.nix \
  --arg busybox "$busybox" \
  --out-link "$TEST_ROOT/result-from-remote" \
  --store "$TEST_ROOT/local" \
  --builders "ssh-ng://localhost?system-features=testf - - 4 1 testf:1" \
| grepQuiet "Failed to find a machine for remote build!"
