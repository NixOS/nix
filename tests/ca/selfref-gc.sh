#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.4pre20210626"

sed -i 's/experimental-features .*/& ca-derivations ca-references nix-command flakes/' "$NIX_CONF_DIR"/nix.conf

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
source ./selfref-gc.sh
