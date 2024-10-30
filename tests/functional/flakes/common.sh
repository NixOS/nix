#!/usr/bin/env bash

source ../common.sh

# shellcheck disable=SC2034 # this variable is used by tests that source this file
registry=$TEST_ROOT/registry.json

writeSimpleFlake() {
    local flakeDir="$1"
    cat > "$flakeDir/flake.nix" <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system = rec {
      foo = import ./simple.nix;
      fooScript = (import ./shell.nix {}).foo;
      default = foo;
    };
    packages.someOtherSystem = rec {
      foo = import ./simple.nix;
      default = foo;
    };

    # To test "nix flake init".
    legacyPackages.$system.hello = import ./simple.nix;

    parent = builtins.dirOf ./.;

    baseName = builtins.baseNameOf ./.;

    root = ./.;
  };
}
EOF

    cp ../simple.nix ../shell.nix ../simple.builder.sh ../config.nix "$flakeDir/"
}

createSimpleGitFlake() {
    local flakeDir="$1"
    writeSimpleFlake "$flakeDir"
    git -C "$flakeDir" add flake.nix simple.nix shell.nix simple.builder.sh config.nix
    git -C "$flakeDir" commit -m 'Initial'
}

writeDependentFlake() {
    local flakeDir="$1"
    cat > "$flakeDir/flake.nix" <<EOF
{
  outputs = { self, flake1 }: {
    packages.$system.default = flake1.packages.$system.default;
    expr = assert builtins.pathExists ./flake.lock; 123;
  };
}
EOF
}

writeTrivialFlake() {
    local flakeDir="$1"
    cat > "$flakeDir/flake.nix" <<EOF
{
  outputs = { self }: {
    expr = 123;
  };
}
EOF
}

createGitRepo() {
    local repo="$1"
    local extraArgs="${2-}"

    rm -rf "$repo" "$repo".tmp
    mkdir -p "$repo"

    # shellcheck disable=SC2086 # word splitting of extraArgs is intended
    git -C "$repo" init $extraArgs
    git -C "$repo" config user.email "foobar@example.com"
    git -C "$repo" config user.name "Foobar"
}
