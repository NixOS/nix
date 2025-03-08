#!/usr/bin/env bash

source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

writeSimpleFlake "$flakeDir"
cd "$flakeDir"


# By default: Only show the packages content for the current system and no
# legacyPackages at all
nix flake show --json > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.output.children.someOtherSystem.filtered;
assert show_output.packages.output.children.${builtins.currentSystem}.children.default.derivationName == "simple";
assert show_output.legacyPackages.skipped;
true
'

# With `--all-systems`, show the packages for all systems
nix flake show --json --all-systems > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.output.children.someOtherSystem.children.default.derivationName == "simple";
assert show_output.legacyPackages.skipped;
true
'

# With `--legacy`, show the legacy packages
nix flake show --json --legacy > show-output.json
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.output.children.${builtins.currentSystem}.children.hello.derivationName == "simple";
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
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.legacyPackages.output.children.${builtins.currentSystem}.children.AAAAAASomeThingsFailToEvaluate.failed;
assert show_output.legacyPackages.output.children.${builtins.currentSystem}.children.simple.derivationName == "simple";
true
'
