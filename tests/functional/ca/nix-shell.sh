#!/usr/bin/env bash

source common.sh

# shellcheck disable=SC2034
NIX_TESTS_CA_BY_DEFAULT=true
cd ..
# shellcheck source=/dev/null
source ./nix-shell.sh
