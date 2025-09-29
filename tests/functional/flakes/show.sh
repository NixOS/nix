#!/usr/bin/env bash

source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

writeSimpleFlake "$flakeDir"
pushd "$flakeDir"


# By default: Only show the packages content for the current system and no
# legacyPackages at all
nix flake show --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.someOtherSystem.default == {};
assert show_output.packages.${builtins.currentSystem}.default.name == "simple";
assert show_output.legacyPackages.${builtins.currentSystem} == {};
true
'

# With `--all-systems`, show the packages for all systems
nix flake show --json --all-systems > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.someOtherSystem.default.name == "simple";
assert show_output.legacyPackages.${builtins.currentSystem} == {};
true
'

# With `--legacy`, show the legacy packages
nix flake show --json --legacy > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.${builtins.currentSystem}.hello.name == "simple";
true
'

# Test that attributes are only reported when they have actual content
cat >flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    apps.$system = { };
    checks.$system = { };
    devShells.$system = { };
    legacyPackages.$system = { };
    packages.$system = { };
    packages.someOtherSystem = { };

    formatter = { };
    nixosConfigurations = { };
    nixosModules = { };
  };
}
EOF
nix flake show --json --all-systems > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output == { };
true
'

# Test that attributes with errors are handled correctly.
# nixpkgs.legacyPackages is a particularly prominent instance of this.
cat >flake.nix <<EOF
{
  outputs = inputs: {
    legacyPackages.$system = {
      AAAAAASomeThingsFailToEvaluate = throw "nooo";
      simple = import ./simple.nix;
    };
  };
}
EOF
nix flake show --json --legacy --all-systems > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.${builtins.currentSystem}.AAAAAASomeThingsFailToEvaluate == { };
assert show_output.legacyPackages.${builtins.currentSystem}.simple.name == "simple";
true
'

# Test that nix flake show doesn't fail if one of the outputs contains
# an IFD
popd
writeIfdFlake "$flakeDir"
pushd "$flakeDir"


nix flake show --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.${builtins.currentSystem}.default == { };
true
'
