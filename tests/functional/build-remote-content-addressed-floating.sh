#!/usr/bin/env bash

source common.sh

file=build-hook-ca-floating.nix

enableFeatures "ca-derivations"

NIX_TESTS_CA_BY_DEFAULT=true

source build-remote.sh
