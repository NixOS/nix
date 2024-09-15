#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

drvPath=$(nix derivation instantiate --no-link --file simple.nix)
test -f "$drvPath"
nix-store --delete "$drvPath"
if test -f "$drvPath"; then false; fi

drvPath=$(nix derivation instantiate --file simple.nix)
test -f "$drvPath"
test -e drv
nix-store --gc --print-roots | grep "$drvPath"
nix-store --gc --print-live | grep "$drvPath"
if nix-store --delete "$drvPath"; then false; fi
test -f "$drvPath"
[ "$(nix-store -q --roots "$drvPath")" = "$(realpath --no-symlinks drv) -> $drvPath" ]
rm drv
nix-store --delete "$drvPath"
if test -f "$drvPath"; then false; fi

drvPath=$(nix derivation instantiate --out-link foobar --file simple.nix)
test -e foobar
[ "$(nix-store -q --roots "$drvPath")" = "$(realpath --no-symlinks foobar) -> $drvPath" ]
rm foobar
nix-store --delete "$drvPath"

drvPathJson=$(nix derivation instantiate --json --no-link --file simple.nix)
[ "$drvPathJson" = "{\"$drvPath\":{}}" ]
nix-store --delete "$drvPath"

mapfile -t drvPaths < <(nix derivation instantiate --json --out-link multidrv --file check.nix | jq 'keys|.[]' -r)
roots=(./multidrv*)
[ "${#roots[@]}" -gt 1 ]
[ "${#roots[@]}" -eq "${#drvPaths[@]}" ]
mapfile -t rootedPaths < <(readlink "${roots[@]}")
[ "${rootedPaths[*]}" = "${drvPaths[*]}" ]
rm -f multidrv*

nix-collect-garbage
