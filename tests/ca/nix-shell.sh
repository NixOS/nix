#!/usr/bin/env bash

source common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references nix-command flakes/' "$NIX_CONF_DIR"/nix.conf

CONTENT_ADDRESSED=true
cd ..
source ./nix-shell.sh

