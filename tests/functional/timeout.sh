#!/usr/bin/env bash

# Test the `--timeout' option.

source common.sh

# XXX: This shouldn't be, but #4813 cause this test to fail
needLocalStore "see #4813"

# Also skip on NixOS: meson sets NIX_REMOTE='' so needLocalStore
# doesn't detect daemon mode, but auto store selection still picks
# UDSRemoteStore because the state dir isn't writable by the test user.
# --max-build-log-size hangs through the daemon (#4813).
if isTestOnNixOS; then
    skipTest "timeout tests don't work reliably through the daemon (see #4813)"
fi

expectStderr 101 nix-build -Q timeout.nix -A infiniteLoop --timeout 2 | grepQuiet "timed out"

expectStderr 1 nix-build -Q timeout.nix -A infiniteLoop --max-build-log-size 100 | grepQuiet "killed after writing more than 100 bytes of log output"

expectStderr 101 nix-build timeout.nix -A silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"

expectStderr 100 nix-build timeout.nix -A closeLog | grepQuiet "builder failed due to signal"

expectStderr 1 nix build -f timeout.nix silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"
