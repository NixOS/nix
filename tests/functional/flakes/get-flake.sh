#!/usr/bin/env bash

source ./common.sh

createFlake1

mkdir -p "$flake1Dir/subflake"
cat > "$flake1Dir/subflake/flake.nix" <<EOF
{
  outputs = { self }:
    let
      # Bad, legacy way of getting a flake from an input.
      parentFlake = builtins.getFlake (builtins.flakeRefToString { type = "path"; path = self.sourceInfo.outPath; narHash = self.narHash; });
      # Better way using a path value.
      parentFlake2 = builtins.getFlake ./..;
    in {
      x = parentFlake.number;
      y = parentFlake2.number;
    };
}
EOF
git -C "$flake1Dir" add subflake/flake.nix

[[ $(nix eval "$flake1Dir/subflake#x") = 123 ]]

[[ $(nix eval "$flake1Dir/subflake#y") = 123 ]]
