#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.6.0pre20211215"

clearStoreIfPossible

# shellcheck disable=SC2016
nix-build --no-out-link -E '
  with import '"${config_nix}"';

  let d1 = mkDerivation {
    name = "selfref-gc";
    outputs = [ "out" ];
    buildCommand = "
      echo SELF_REF: $out > $out
    ";
  }; in

  # the only change from d1 is d1 as an (unused) build input
  # to get identical store path in CA.
  mkDerivation {
    name = "selfref-gc";
    outputs = [ "out" ];
    buildCommand = "
      echo UNUSED: ${d1}
      echo SELF_REF: $out > $out
    ";
  }
'

nix-collect-garbage
