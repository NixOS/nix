#!/usr/bin/env bash

source common.sh

NIX_TESTS_CA_BY_DEFAULT=true
cd ..
source ./nix-shell.sh
