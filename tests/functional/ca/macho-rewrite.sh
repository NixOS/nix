#!/usr/bin/env bash

# CA-derivation variant of `macho-rewrite.sh`. The main test
# branches on NIX_TESTS_CA_BY_DEFAULT to handle the CA trigger shape.

source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
# shellcheck source=/dev/null
source ./macho-rewrite.sh
