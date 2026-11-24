#!/usr/bin/env bash

source common.sh

file=build-hook-ca-floating.nix

enableFeatures "ca-derivations"

# FIXME: version 2 derivations cannot yet be sent to remote builders.
skipTest "version 2 derivations over the wire"

NIX_TESTS_CA_BY_DEFAULT=true

source build-remote.sh
