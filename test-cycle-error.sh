#!/usr/bin/env bash

export TERM=dumb


if ! [ -f outputs/out/libexec/nix/build-remote ]; then
  mkdir -p outputs/out/libexec/nix/
  (
    cd outputs/out/libexec/nix/
    ln -s ../../bin/nix build-remote
  )
fi

make tests/multiple-outputs.sh.test

exit 0

# src/nix/nix == outputs/out/bin/nix ?

# options for nix
o=()
o+=(--impure --builders '' --expr 'with import <nixpkgs> { }; (pkgs.stdenv.mkDerivation { name = "cycle"; outputs = [ "a" "b" "c" ]; builder = (pkgs.writeShellApplication{ name = "builder.sh"; runtimeInputs = [ pkgs.busybox ]; checkPhase = ":"; text = "mkdir $a $b $c; echo $a > $b/a; echo $b > $c/b; echo $c > $a/c"; }) + "/bin/builder.sh"; }).a')

#if false; then
if true; then
  #if true; then
  if false; then
    [ -e ./outputs/out/bin/nix-build ] || ln -s nix ./outputs/out/bin/nix-build
    ./outputs/out/bin/nix-build "${o[@]}"
  else
    ./outputs/out/bin/nix --extra-experimental-features nix-command build "${o[@]}"
  fi
else
  ./src/nix/nix --extra-experimental-features nix-command build "${o[@]}"
fi
