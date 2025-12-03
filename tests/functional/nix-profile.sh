#!/usr/bin/env bash

source common.sh

TODO_NixOS

clearStore
clearProfiles

enableFeatures "ca-derivations"
restartDaemon

# Make a flake.
flake1Dir=$TEST_ROOT/flake1
mkdir -p "$flake1Dir"

# shellcheck disable=SC2154,SC1039
cat > "$flake1Dir"/flake.nix <<EOF
{
  description = "Bla bla";

  outputs = { self }: with import ./config.nix; rec {
    packages.$system.default = mkDerivation {
      name = "profile-test-\${builtins.readFile ./version}";
      outputs = [ "out" "man" "dev" ];
      builder = builtins.toFile "builder.sh"
        ''
          mkdir -p \$out/bin
          cat > \$out/bin/hello <<EOF
          #! ${shell}
          echo Hello \${builtins.readFile ./who}
          EOF
          chmod +x \$out/bin/hello
          echo DONE
          mkdir -p \$man/share/man
          mkdir -p \$dev/include
        '';
      __contentAddressed = import ./ca.nix;
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
      meta.outputsToInstall = [ "out" "man" ];
    };
  };
}
EOF

printf World > "$flake1Dir"/who
printf 1.0 > "$flake1Dir"/version
printf false > "$flake1Dir"/ca.nix

cp "${config_nix}" "$flake1Dir"/

# Test upgrading from nix-env.
nix-env -f ./user-envs.nix -i foo-1.0
nix profile list | grep -A2 'Name:.*foo' | grep 'Store paths:.*foo-1.0'
nix profile add "$flake1Dir" -L
nix profile list | grep -A4 'Name:.*flake1' | grep 'Locked flake URL:.*narHash'
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World" ]]
[ -e "$TEST_HOME"/.nix-profile/share/man ]
# shellcheck disable=SC2235
(! [ -e "$TEST_HOME"/.nix-profile/include ])
nix profile history
nix profile history | grep "packages.$system.default: ∅ -> 1.0"
nix profile diff-closures | grep 'env-manifest.nix: ε → ∅'

# Test XDG Base Directories support
export NIX_CONFIG="use-xdg-base-directories = true"
nix profile remove flake1 2>&1 | grep 'removed 1 packages'
nix profile add "$flake1Dir"
[[ $("$TEST_HOME"/.local/state/nix/profile/bin/hello) = "Hello World" ]]
unset NIX_CONFIG

# Test conflicting package add.
nix profile add "$flake1Dir" 2>&1 | grep "warning: 'flake1' is already added"

# Test upgrading a package.
printf NixOS > "$flake1Dir"/who
printf 2.0 > "$flake1Dir"/version
nix profile upgrade flake1
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello NixOS" ]]
nix profile history | grep "packages.$system.default: 1.0, 1.0-man -> 2.0, 2.0-man"

# Test upgrading package using regular expression.
printf 2.1 > "$flake1Dir"/version
nix profile upgrade --regex '.*'
[[ $(readlink "$TEST_HOME"/.nix-profile/bin/hello) =~ .*-profile-test-2\.1/bin/hello ]]
nix profile rollback

# Test upgrading all packages
printf 2.2 > "$flake1Dir"/version
nix profile upgrade --all
[[ $(readlink "$TEST_HOME"/.nix-profile/bin/hello) =~ .*-profile-test-2\.2/bin/hello ]]
nix profile rollback
printf 1.0 > "$flake1Dir"/version

# Test --all exclusivity.
assertStderr nix --offline profile upgrade --all foo << EOF
error: --all cannot be used with package names or regular expressions.
Try 'nix --help' for more information.
EOF

# Test matching no packages using literal package name.
assertStderr nix --offline profile upgrade this_package_is_not_installed << EOF
warning: Package name 'this_package_is_not_installed' does not match any packages in the profile.
warning: No packages to upgrade. Use 'nix profile list' to see the current profile.
EOF

# Test matching no packages using regular expression.
assertStderr nix --offline profile upgrade --regex '.*unknown_package.*' << EOF
warning: Regex '.*unknown_package.*' does not match any packages in the profile.
warning: No packages to upgrade. Use 'nix profile list' to see the current profile.
EOF

# Test removing all packages using regular expression.
nix profile remove --regex '.*' 2>&1 | grep "removed 2 packages, kept 0 packages"
nix profile rollback

# Test 'history', 'diff-closures'.
nix profile diff-closures

# Test rollback.
printf World > "$flake1Dir"/who
nix profile upgrade flake1
printf NixOS > "$flake1Dir"/who
nix profile upgrade flake1
nix profile rollback
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World" ]]

# Test uninstall.
[ -e "$TEST_HOME"/.nix-profile/bin/foo ]
# shellcheck disable=SC2235
nix profile remove foo 2>&1 | grep 'removed 1 packages'
# shellcheck disable=SC2235
(! [ -e "$TEST_HOME"/.nix-profile/bin/foo ])
nix profile history | grep 'foo: 1.0 -> ∅'
nix profile diff-closures | grep 'Version 3 -> 4'

# Test installing a non-flake package.
nix profile add --file ./simple.nix ''
[[ $(cat "$TEST_HOME"/.nix-profile/hello) = "Hello World!" ]]
nix profile remove simple 2>&1 | grep 'removed 1 packages'
nix profile add "$(nix-build --no-out-link ./simple.nix)"
[[ $(cat "$TEST_HOME"/.nix-profile/hello) = "Hello World!" ]]

# Test packages with same name from different sources
mkdir "$TEST_ROOT"/simple-too
cp ./simple.nix "${config_nix}" simple.builder.sh "$TEST_ROOT"/simple-too
nix profile add --file "$TEST_ROOT"/simple-too/simple.nix ''
nix profile list | grep -A4 'Name:.*simple' | grep 'Name:.*simple-1'
nix profile remove simple 2>&1 | grep 'removed 1 packages'
nix profile remove simple-1 2>&1 | grep 'removed 1 packages'

# Test wipe-history.
nix profile wipe-history
[[ $(nix profile history | grep -c Version) -eq 1 ]]

# Test upgrade to CA package.
printf true > "$flake1Dir"/ca.nix
printf 3.0 > "$flake1Dir"/version
nix profile upgrade flake1
nix profile history | grep "packages.$system.default: 1.0, 1.0-man -> 3.0, 3.0-man"

# Test new install of CA package.
nix profile remove flake1 2>&1 | grep 'removed 1 packages'
printf 4.0 > "$flake1Dir"/version
printf Utrecht > "$flake1Dir"/who
nix profile add "$flake1Dir"
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello Utrecht" ]]
nix path-info --json "$(realpath "$TEST_HOME"/.nix-profile/bin/hello)" | jq -e '.[].ca | .method == "nar" and .hash.algorithm == "sha256"'

# Override the outputs.
nix profile remove simple flake1
nix profile add "$flake1Dir^*"
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello Utrecht" ]]
[ -e "$TEST_HOME"/.nix-profile/share/man ]
[ -e "$TEST_HOME"/.nix-profile/include ]

printf Nix > "$flake1Dir"/who
nix profile list
nix profile upgrade flake1
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello Nix" ]]
[ -e "$TEST_HOME"/.nix-profile/share/man ]
[ -e "$TEST_HOME"/.nix-profile/include ]

nix profile remove flake1 2>&1 | grep 'removed 1 packages'
nix profile add "$flake1Dir^man"
# shellcheck disable=SC2235
(! [ -e "$TEST_HOME"/.nix-profile/bin/hello ])
[ -e "$TEST_HOME"/.nix-profile/share/man ]
# shellcheck disable=SC2235
(! [ -e "$TEST_HOME"/.nix-profile/include ])

# test priority
nix profile remove flake1 2>&1 | grep 'removed 1 packages'

# Make another flake.
flake2Dir=$TEST_ROOT/flake2
printf World > "$flake1Dir"/who
cp -r "$flake1Dir" "$flake2Dir"
printf World2 > "$flake2Dir"/who

nix profile add "$flake1Dir"
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World" ]]
expect 1 nix profile add "$flake2Dir"
diff -u <(
    nix --offline profile install "$flake2Dir" 2>&1 1> /dev/null \
        | grep -vE "^warning: " \
        | grep -vE "^error \(ignored\): " \
        || true
) <(cat << EOF
error: An existing package already provides the following file:

         $(nix build --no-link --print-out-paths "${flake1Dir}""#default.out")/bin/hello

       This is the conflicting file from the new package:

         $(nix build --no-link --print-out-paths "${flake2Dir}""#default.out")/bin/hello

       To remove the existing package:

         nix profile remove flake1

       The new package can also be added next to the existing one by assigning a different priority.
       The conflicting packages have a priority of 5.
       To prioritise the new package:

         nix profile add path:${flake2Dir}#packages.${system}.default --priority 4

       To prioritise the existing package:

         nix profile add path:${flake2Dir}#packages.${system}.default --priority 6
EOF
)
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World" ]]
nix profile add "$flake2Dir" --priority 100
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World" ]]
nix profile add "$flake2Dir" --priority 0
[[ $("$TEST_HOME"/.nix-profile/bin/hello) = "Hello World2" ]]
# nix profile add $flake1Dir --priority 100
# [[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]

# Ensure that conflicts are handled properly even when the installables aren't
# flake references.
# Regression test for https://github.com/NixOS/nix/issues/8284
clearProfiles
# shellcheck disable=SC2046
nix profile add $(nix build "$flake1Dir" --no-link --print-out-paths)
expect 1 nix profile add --impure --expr "(builtins.getFlake ''$flake2Dir'').packages.$system.default"

# Test upgrading from profile version 2.
clearProfiles
mkdir -p "$TEST_ROOT"/import-profile
outPath=$(nix build --no-link --print-out-paths "$flake1Dir"/flake.nix^out)
printf '{ "version": 2, "elements": [ { "active": true, "attrPath": "legacyPackages.x86_64-linux.hello", "originalUrl": "flake:nixpkgs", "outputs": null, "priority": 5, "storePaths": [ "%s" ], "url": "github:NixOS/nixpkgs/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" } ] }' "$outPath" > "$TEST_ROOT"/import-profile/manifest.json
nix build --profile "$TEST_HOME"/.nix-profile "$(nix store add-path "$TEST_ROOT"/import-profile)" --no-link
nix profile list | grep -A4 'Name:.*hello' | grep "Store paths:.*$outPath"
nix profile remove hello 2>&1 | grep 'removed 1 packages, kept 0 packages'
