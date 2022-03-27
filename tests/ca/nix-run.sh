#!/usr/bin/env bash

source common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references nix-command flakes/' "$NIX_CONF_DIR"/nix.conf

FLAKE_PATH=path:$PWD

nix run --no-write-lock-file $FLAKE_PATH#runnable
