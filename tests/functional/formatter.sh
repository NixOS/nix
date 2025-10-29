#!/usr/bin/env bash

source common.sh

TODO_NixOS # Provide a `shell` variable. Try not to `export` it, perhaps.

clearStoreIfPossible
rm -rf "$TEST_HOME"/.cache "$TEST_HOME"/.config "$TEST_HOME"/.local

cp ./simple.nix ./simple.builder.sh ./formatter.simple.sh "${config_nix}" "$TEST_HOME"

cd "$TEST_HOME"

nix formatter --help | grep "build or run the formatter"
nix fmt --help | grep "reformat your code"
nix fmt run --help | grep "reformat your code"
nix fmt build --help | grep "build"

# shellcheck disable=SC2154
cat << EOF > flake.nix
{
  outputs = _: {
    formatter.$system =
      with import ./config.nix;
      mkDerivation {
        name = "formatter";
        buildCommand = ''
          mkdir -p \$out/bin
          echo "#! ${shell}" > \$out/bin/formatter
          cat \${./formatter.simple.sh} >> \$out/bin/formatter
          chmod +x \$out/bin/formatter
        '';
      };
  };
}
EOF

mkdir subflake
cp ./simple.nix ./simple.builder.sh ./formatter.simple.sh "${config_nix}" "$TEST_HOME/subflake"

cat << EOF > subflake/flake.nix
{
  outputs = _: {
    formatter.$system =
      with import ./config.nix;
      mkDerivation {
        name = "formatter";
        buildCommand = ''
          mkdir -p \$out/bin
          echo "#! ${shell}" > \$out/bin/formatter
          cat \${./formatter.simple.sh} >> \$out/bin/formatter
          chmod +x \$out/bin/formatter
        '';
      };
  };
}
EOF

# No arguments check
[[ "$(nix fmt)" = "PRJ_ROOT=$TEST_HOME Formatting(0):" ]]
[[ "$(nix formatter run)" = "PRJ_ROOT=$TEST_HOME Formatting(0):" ]]

# Argument forwarding check
nix fmt ./file ./folder | grep "PRJ_ROOT=$TEST_HOME Formatting(2): ./file ./folder"
nix formatter run ./file ./folder | grep "PRJ_ROOT=$TEST_HOME Formatting(2): ./file ./folder"

# test subflake
cd subflake
nix fmt ./file | grep "PRJ_ROOT=$TEST_HOME/subflake Formatting(1): ./file"

# Build checks
## Defaults to a ./result.
nix formatter build | grep ".\+/bin/formatter"
[[ -L ./result ]]
rm result

## Can prevent the symlink.
nix formatter build --no-link
[[ ! -e ./result ]]

## Can change the symlink name.
nix formatter build --out-link my-result | grep ".\+/bin/formatter"
[[ -L ./my-result ]]
rm ./my-result

# Flake outputs check.
nix flake check
nix flake show | grep -P "package 'formatter'"
