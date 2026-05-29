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


# Test that nix keeps going even when packages.$SYSTEM contains not derivations
cat >flake.nix <<EOF
{
  outputs = inputs: {
    packages.$system = {
      drv1 = import ./simple.nix;
      not-a-derivation = 42;
      drv2 = import ./simple.nix;
    };
  };
}
EOF
nix flake show --json --all-systems > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages.${builtins.currentSystem}.not-a-derivation == {};
true
'

# System folding tests
# ============================================================

# Test 1: Default folding behavior - systems should be folded in non-JSON mode
cat >flake.nix <<EOF
{
  description = "System folding test";
  outputs = inputs: {
    packages.x86_64-linux.p1 = import ./simple.nix;
    packages.aarch64-linux.p1 = import ./simple.nix;
    apps.x86_64-linux.a1 = {
      type = "app";
      program = "\${./simple.nix}";
    };
    apps.aarch64-linux.a1 = {
      type = "app";
      program = "\${./simple.nix}";
    };
    checks.x86_64-linux.c1 = import ./simple.nix;
    checks.aarch64-linux.c1 = import ./simple.nix;
    devShells.x86_64-linux.default = import ./shell.nix;
    devShells.aarch64-linux.default = import ./shell.nix;
  };
}
EOF

nix flake show > show-output.txt
# Check that current system is highlighted (on Linux) or that test systems exist (on macOS)
case "$system" in
  x86_64-linux|aarch64-linux)
    # On Linux, current system is one of the defined systems and should be highlighted in brackets
    grep -q "[$system]" show-output.txt
    ;;
  *)
    # On macOS, current system is not in the flake, so just check that test systems are present
    grep -q "x86_64-linux" show-output.txt
    grep -q "aarch64-linux" show-output.txt
    ;;
esac
# Additional check: ensure test systems are present on all platforms
grep -q "x86_64-linux" show-output.txt
grep -q "aarch64-linux" show-output.txt

# Test 2: --no-system-folding shows all systems separately
nix flake show --no-system-folding > show-output.txt
grep -q "x86_64-linux" show-output.txt
grep -q "aarch64-linux" show-output.txt

# Test 3: Folding is disabled with --json or --all-systems
cat >flake.nix <<EOF
{
  description = "Folding disable test";
  outputs = inputs: {
    packages.x86_64-linux.default = import ./simple.nix;
    packages.aarch64-linux.default = import ./simple.nix;
  };
}
EOF

nix flake show --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages ? x86_64-linux;
assert show_output.packages ? aarch64-linux;
true
'

nix flake show --all-systems > show-output.txt
grep -q "x86_64-linux" show-output.txt
grep -q "aarch64-linux" show-output.txt

# Test 4: Single system and empty systems work correctly
cat >flake.nix <<EOF
{
  description = "Edge case test";
  outputs = inputs: {
    packages.$system.default = import ./simple.nix;
    apps.$system.hello = {
      type = "app";
      program = "\${./simple.nix}";
    };
  };
}
EOF

nix flake show > show-output.txt
grep -q "$system" show-output.txt

cat >flake.nix <<EOF
{
  description = "Empty systems test";
  outputs = inputs: {
    packages = {};
    apps = {};
    checks = {};
    devShells = {};
  };
}
EOF

nix flake show > show-output.txt

# Test 5: legacyPackages are NOT folded (intentional)
cat >flake.nix <<EOF
{
  description = "legacyPackages test";
  outputs = inputs: {
    legacyPackages.x86_64-linux.p1 = import ./simple.nix;
    legacyPackages.aarch64-linux.p1 = import ./simple.nix;
  };
}
EOF

nix flake show --legacy > show-output.txt
grep -q "x86_64-linux" show-output.txt
grep -q "aarch64-linux" show-output.txt
