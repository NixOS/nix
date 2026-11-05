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

# Check backward compatibility with getFlake applied to a store path with discarded string context.
mkdir -p "$flake1Dir/a/b"
cat > "$flake1Dir/a/b/flake.nix" <<EOF
{
  outputs = { self }: { foo = "baz"; };
}
EOF
git -C "$flake1Dir" add a/b/flake.nix

cat > "$flake1Dir/flake.nix" <<EOF
{
  outputs =
    { self }:
    let
      flakeOutputs = builtins.getFlake (
        builtins.unsafeDiscardStringContext "path:\${self.sourceInfo}?narHash=\${self.narHash}"
      );
      subflakeOutputs = builtins.getFlake (
        builtins.unsafeDiscardStringContext "path:\${self.sourceInfo}?dir=a/b&narHash=\${self.narHash}"
      );
    in
    {
      foo = "bar";
      inherit flakeOutputs subflakeOutputs;
    };
}
EOF

[[ $(nix eval --raw "$flake1Dir#flakeOutputs.foo") = bar ]]

# A multi-component 'dir' used to trigger an assertion failure in CanonPath::push.
[[ $(nix eval --raw "$flake1Dir#subflakeOutputs.foo") = baz ]]
