#!/usr/bin/env bash

source common.sh

# Store layer needs bugfix
requireDaemonNewerThan "2.30pre20250515"

expected=100
if [[ -v NIX_DAEMON_PACKAGE ]]; then expected=1; fi # work around the daemon not returning a 100 status correctly

expectStderr "$expected" nix-build ./text-hashed-output.nix -A failingWrapper --no-out-link \
    | grepQuiet "build of resolved derivation '.*use-dynamic-drv-in-non-dynamic-drv-wrong.drv' failed"
