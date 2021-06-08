#!/usr/bin/env bash

source common.sh

code='builtins.getAttr "nope" (builtins.listToAttrs [ { name = "foo"; value = "bar"; } ])'

expect 1 nix-instantiate -E "$code"
