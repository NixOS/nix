# shellcheck shell=bash

EDITOR='cat' nix repl <<< ':e derivation' 2>&1 | grepQuiet 'derivationStrict'
EDITOR='cat' nix repl <<< ':e <nix/fetchurl.nix>' 2>&1 | grepQuiet 'builtin:fetchurl'
