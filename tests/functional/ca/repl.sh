#!/usr/bin/env bash

source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1
# shellcheck source=/dev/null
cd .. && source repl.sh
