#!/usr/bin/env bash

source common.sh

FLAKE_PATH=path:$PWD

nix run --no-write-lock-file $FLAKE_PATH#runnable
