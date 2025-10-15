#!/usr/bin/env bash

# Ensure that garbage collection works properly with ca derivations

source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1

cd ..
# shellcheck source=/dev/null
source eval-store.sh
