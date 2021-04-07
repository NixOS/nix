#!/usr/bin/env bash

source common.sh

feats=(--experimental-features 'nix-command ca-derivations')

nix eval "${feats[@]}" --impure --expr \
    'with (import ./text-hashed-output.nix); let
       a = root.outPath;
       b = builtins.outputOf root.drvPath "out";
     in builtins.trace a
       (builtins.trace b
         (assert a == b; null))'

nix eval "${feats[@]}" --impure --expr \
    'with (import ./text-hashed-output.nix); let
       a = dependent.outPath;
       b = builtins.outputOf dependent.drvPath "out";
     in builtins.trace a
       (builtins.trace b
         (assert a == b; null))'

nix eval "${feats[@]}" --impure --expr \
    'with (import ./text-hashed-output.nix); let
       a = builtins.outputOf dependent.outPath "out";
       b = builtins.outputOf dependent.out "out";
     in builtins.trace a
       (builtins.trace b
         (assert a == b; null))'

nix eval "${feats[@]}" --impure --expr \
    'with (import ./text-hashed-output.nix); let
       a = builtins.outputOf dependent.out "out";
	   b = builtins.outputOf (builtins.outputOf dependent.drvPath "out") "out";
     in builtins.trace a
       (builtins.trace b
         (assert a == b; null))'
