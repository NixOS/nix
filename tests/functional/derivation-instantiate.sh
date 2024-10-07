#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore

drvPath=$(nix derivation instantiate --no-link --file simple.nix)
test -f "$drvPath"
nix-store --delete "$drvPath"
if test -f "$drvPath"; then false; fi

rm -f drv
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

rm -f foobar
drvPath=$(nix derivation instantiate --out-link foobar --file simple.nix)
test -e foobar
[ "$(nix-store -q --roots "$drvPath")" = "$(realpath --no-symlinks foobar) -> $drvPath" ]
rm foobar
nix-store --delete "$drvPath"

drvPathJson=$(nix derivation instantiate --json --no-link --file simple.nix)
[ "$drvPathJson" = "[{\"drvPath\":\"$drvPath\"}]" ]
nix-store --delete "$drvPath"

rm -f multidrv*
mapfile -t drvPaths < <(nix derivation instantiate --json --out-link multidrv --file check.nix | jq '.[]|.drvPath' -r)
roots=(./multidrv*)
[ "${#roots[@]}" -gt 1 ]
[ "${#roots[@]}" -eq "${#drvPaths[@]}" ]
mapfile -t rootedPaths < <(readlink "${roots[@]}")
[ "${rootedPaths[*]}" = "${drvPaths[*]}" ]
rm -f multidrv*

# The order should always be the same in text and json outputs
jsonOutput=$(nix derivation instantiate --no-link --file check.nix --json | jq '.[]|.drvPath' -r)
textOutput=$(nix derivation instantiate --no-link --file check.nix)
[ "$jsonOutput" = "$textOutput" ]

# Test that the order is the same as on the command line, and that repeated
# inputs are present several times in the output, in the correct order
nix derivation instantiate --no-link --file multiple-outputs.nix a b a --json | jq --exit-status '
      (.[0].drvPath | match(".*multiple-outputs-a.drv"))
  and (.[1].drvPath | match(".*multiple-outputs-b.drv"))
  and (.[2].drvPath | match(".*multiple-outputs-a.drv"))
'

nix-collect-garbage
