#!/usr/bin/env bash

source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1

cd .. && source why-depends.sh
