#!/usr/bin/env bash

source common.sh

TODO_NixOS # Provide a `shell` variable. Try not to `export` it, perhaps.

clearStoreIfPossible
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./fmt.simple.sh ./config.nix $TEST_HOME

cd $TEST_HOME

nix fmt --help | grep "Format"

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
          cat \${./fmt.simple.sh} >> \$out/bin/formatter
          chmod +x \$out/bin/formatter
        '';
      };
  };
}
EOF
nix fmt ./file ./folder | grep 'Formatting: ./file ./folder'
nix flake check
nix flake show | grep -P "package 'formatter'"
