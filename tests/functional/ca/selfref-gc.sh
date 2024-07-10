#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.4pre20210626"

enableFeatures "ca-derivations nix-command"

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
source ./selfref-gc.sh
