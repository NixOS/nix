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
#
# These tests rely on `$system` being the host running the test. Folding
# prefers the local system, so the asserted output depends on whether the host
# system is among the ones advertised by the flake.

# A flake advertising both x86_64-linux and aarch64-linux across the foldable
# categories.
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

# Test 1: by default the two systems are folded into a single label that lists
# both systems inside one pair of braces.
nix flake show > show-output.txt
grep -qE '\{[^}]*x86_64-linux[^}]*aarch64-linux[^}]*\}' show-output.txt

# When the host system is one of the advertised systems, folding must descend
# into the local system so its content is shown (not "omitted"). This is the
# regression guard for the bug where the descended system was chosen by display
# order rather than by locality.
case "$system" in
  x86_64-linux|aarch64-linux)
    grepQuietInverse 'omitted' show-output.txt
    ;;
esac

# Test 2: --no-system-folding shows each system as its own node (no braces).
nix flake show --no-system-folding > show-output.txt
# No folded braces: systems are not consolidated.
grepQuietInverse '{' show-output.txt
# Both systems appear. They are bold-wrapped node labels here, so they are
# present as substrings.
grep -q 'x86_64-linux' show-output.txt
grep -q 'aarch64-linux' show-output.txt

# Test 3: folding is disabled with --json and with --all-systems.
nix flake show --json > show-output.json
# shellcheck disable=SC2016
nix eval --impure --expr '
let show_output = builtins.fromJSON (builtins.readFile ./show-output.json);
in
assert show_output.packages ? x86_64-linux;
assert show_output.packages ? aarch64-linux;
assert show_output.apps ? x86_64-linux;
assert show_output.apps ? aarch64-linux;
true
'

nix flake show --all-systems > show-output.txt
grepQuietInverse '{' show-output.txt
grep -q 'x86_64-linux' show-output.txt
grep -q 'aarch64-linux' show-output.txt

# Test 4: a single system is not folded (no braces around a lone system).
cat >flake.nix <<EOF
{
  description = "Single system test";
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
grepQuietInverse '{' show-output.txt
grep -q "$system" show-output.txt

# Test 5: empty system categories don't crash and produce no folded node.
cat >flake.nix <<EOF
{
  description = "Empty categories test";
  outputs = inputs: {
    packages = {};
    apps = {};
    checks = {};
    devShells = {};
  };
}
EOF
nix flake show > show-output.txt

# Test 6: legacyPackages is never folded even with multiple systems.
cat >flake.nix <<EOF
{
  description = "legacyPackages not folded";
  outputs = inputs: {
    legacyPackages.x86_64-linux.p1 = import ./simple.nix;
    legacyPackages.aarch64-linux.p1 = import ./simple.nix;
  };
}
EOF
nix flake show --legacy > show-output.txt
grepQuietInverse '{' show-output.txt
grep -q 'x86_64-linux' show-output.txt
grep -q 'aarch64-linux' show-output.txt
