#!/usr/bin/env bash

source common.sh

TODO_NixOS # NixOS doesn't provide $NIX_STATE_DIR (and shouldn't)

clearStore

outPath=$(nix-build --no-out-link readfile-context.nix)

# Set a GC root.
ln -s "$outPath" "$NIX_STATE_DIR/gcroots/foo"

# Check that file exists.
[ "$(cat "$(cat "$outPath")")" = "Hello World!" ]

nix-collect-garbage

# Check that file still exists.
[ "$(cat "$(cat "$outPath")")" = "Hello World!" ]
