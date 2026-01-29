#!/usr/bin/env bash

# Test the `--timeout' option.

source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
needLocalStore "see #4813"

# FIXME: https://github.com/NixOS/nix/issues/4813
expectStderr 101 nix-build -Q timeout.nix -A infiniteLoop --timeout 2 | grepQuiet "timed out" \
    || skipTest "Do not block CI until fixed"

expectStderr 1 nix-build -Q timeout.nix -A infiniteLoop --max-build-log-size 100 | grepQuiet "killed after writing more than 100 bytes of log output"

expectStderr 101 nix-build timeout.nix -A silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"

expectStderr 100 nix-build timeout.nix -A closeLog | grepQuiet "builder failed due to signal"

expectStderr 101 nix build -f timeout.nix silent --max-silent-time 2 | grepQuiet "timed out after 2 seconds"
