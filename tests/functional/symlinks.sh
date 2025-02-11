#!/usr/bin/env bash

source common.sh

# Check that when we have a derivation attribute that refers to a
# symlink, we copy the symlink, not its target.
# shellcheck disable=SC2016
nix build --impure --no-link --expr '
  with import ./config.nix;

  mkDerivation {
    name = "simple";
    builder = builtins.toFile "builder.sh" "[[ -L \"$symlink\" ]]; mkdir $out";
    symlink = ./lang/symlink-resolution/foo/overlays;
  }
'
