#!/usr/bin/env bash

# Test the `--timeout' option.

source common.sh

requireDaemonNewerThan "2.35pre"

expectStderr 101 nix-build -Q timeout.nix -A infiniteLoop --timeout 2 | grepQuiet "timed out"

# When this test runs in the NixOS VM, builds go through the daemon as an
# untrusted user. The daemon ignores client-provided max-build-log-size in that
# mode, so the builder below would run until the Meson test timeout.
if ! isTestOnNixOS; then
    expectStderr 1 nix-build -Q timeout.nix -A infiniteLoop --max-build-log-size 100 | grepQuiet "killed after writing more than 100 bytes of log output"
fi

expectStderr 101 nix-build timeout.nix -A silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"

expectStderr 100 nix-build timeout.nix -A closeLog | grepQuiet "builder failed due to signal"

expectStderr 1 nix build -f timeout.nix silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"
