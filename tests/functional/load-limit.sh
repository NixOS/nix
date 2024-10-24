#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

outPath=$(nix-build --load-limit 100 load-limit.nix --no-out-link)
text=$(cat "$outPath")
if test "$text" != "100"; then exit 1; fi

clearStore

outPath=$(nix-build --load-limit 0 load-limit.nix --no-out-link)
text=$(cat "$outPath")
if test "$text" != "0"; then exit 1; fi

clearStore

outPath=$(nix-build --load-limit none load-limit.nix --no-out-link)
text=$(cat "$outPath")
if test "$text" != "unset"; then exit 1; fi
