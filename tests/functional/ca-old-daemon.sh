#!/usr/bin/env bash

# A daemon too old to report content-addressed realisations should not crash `nix-build`

source common.sh

TODO_NixOS

enableFeatures "ca-derivations"
export NIX_TESTS_CA_BY_DEFAULT=1
restartDaemon

if isDaemonNewer "2.35.0pre20260303"; then
    nix-build dependencies.nix --no-out-link
else
    expectStderr 1 nix-build dependencies.nix --no-out-link |
        grepQuiet "cannot operate on output"
fi
