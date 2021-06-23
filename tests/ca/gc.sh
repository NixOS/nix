#!/usr/bin/env bash

# Ensure that garbage collection works properly with ca derivations

source common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

export NIX_TESTS_CA_BY_DEFAULT=1

cd ..
source gc.sh
