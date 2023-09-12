source common.sh

clearStore
clearProfiles

enableFeatures "ca-derivations"
restartDaemon

# Make a flake.
flake1Dir=$TEST_ROOT/flake1
mkdir -p $flake1Dir

cat > $flake1Dir/flake.nix <<EOF
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

printf World > $flake1Dir/who
printf 1.0 > $flake1Dir/version
printf false > $flake1Dir/ca.nix

cp ./config.nix $flake1Dir/

# Test upgrading from nix-env.
nix-env -f ./user-envs.nix -i foo-1.0
nix profile list | grep -A2 'Index:.*0' | grep 'Store paths:.*foo-1.0'
nix profile install $flake1Dir -L
nix profile list | grep -A4 'Index:.*1' | grep 'Locked flake URL:.*narHash'
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
[ -e $TEST_HOME/.nix-profile/share/man ]
(! [ -e $TEST_HOME/.nix-profile/include ])
nix profile history
nix profile history | grep "packages.$system.default: ∅ -> 1.0"
nix profile diff-closures | grep 'env-manifest.nix: ε → ∅'

# Test XDG Base Directories support

export NIX_CONFIG="use-xdg-base-directories = true"
nix profile remove 1
nix profile install $flake1Dir
[[ $($TEST_HOME/.local/state/nix/profile/bin/hello) = "Hello World" ]]
unset NIX_CONFIG

# Test upgrading a package.
printf NixOS > $flake1Dir/who
printf 2.0 > $flake1Dir/version
nix profile upgrade 1
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello NixOS" ]]
nix profile history | grep "packages.$system.default: 1.0, 1.0-man -> 2.0, 2.0-man"

# Test 'history', 'diff-closures'.
nix profile diff-closures

# Test rollback.
nix profile rollback
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]

# Test uninstall.
[ -e $TEST_HOME/.nix-profile/bin/foo ]
nix profile remove 0
(! [ -e $TEST_HOME/.nix-profile/bin/foo ])
nix profile history | grep 'foo: 1.0 -> ∅'
nix profile diff-closures | grep 'Version 3 -> 4'

# Test installing a non-flake package.
nix profile install --file ./simple.nix ''
[[ $(cat $TEST_HOME/.nix-profile/hello) = "Hello World!" ]]
nix profile remove 1
nix profile install $(nix-build --no-out-link ./simple.nix)
[[ $(cat $TEST_HOME/.nix-profile/hello) = "Hello World!" ]]

# Test wipe-history.
nix profile wipe-history
[[ $(nix profile history | grep Version | wc -l) -eq 1 ]]

# Test upgrade to CA package.
printf true > $flake1Dir/ca.nix
printf 3.0 > $flake1Dir/version
nix profile upgrade 0
nix profile history | grep "packages.$system.default: 1.0, 1.0-man -> 3.0, 3.0-man"

# Test new install of CA package.
nix profile remove 0
printf 4.0 > $flake1Dir/version
printf Utrecht > $flake1Dir/who
nix profile install $flake1Dir
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello Utrecht" ]]
[[ $(nix path-info --json $(realpath $TEST_HOME/.nix-profile/bin/hello) | jq -r .[].ca) =~ fixed:r:sha256: ]]

# Override the outputs.
nix profile remove 0 1
nix profile install "$flake1Dir^*"
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello Utrecht" ]]
[ -e $TEST_HOME/.nix-profile/share/man ]
[ -e $TEST_HOME/.nix-profile/include ]

printf Nix > $flake1Dir/who
nix profile upgrade 0
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello Nix" ]]
[ -e $TEST_HOME/.nix-profile/share/man ]
[ -e $TEST_HOME/.nix-profile/include ]

nix profile remove 0
nix profile install "$flake1Dir^man"
(! [ -e $TEST_HOME/.nix-profile/bin/hello ])
[ -e $TEST_HOME/.nix-profile/share/man ]
(! [ -e $TEST_HOME/.nix-profile/include ])

# test priority
nix profile remove 0

# Make another flake.
flake2Dir=$TEST_ROOT/flake2
printf World > $flake1Dir/who
cp -r $flake1Dir $flake2Dir
printf World2 > $flake2Dir/who

nix profile install $flake1Dir
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
expect 1 nix profile install $flake2Dir
diff -u <(
    nix --offline profile install $flake2Dir 2>&1 1> /dev/null \
        | grep -vE "^warning: " \
        | grep -vE "^error \(ignored\): " \
        || true
) <(cat << EOF
error: An existing package already provides the following file:

         $(nix build --no-link --print-out-paths ${flake1Dir}"#default.out")/bin/hello

       This is the conflicting file from the new package:

         $(nix build --no-link --print-out-paths ${flake2Dir}"#default.out")/bin/hello

       To remove the existing package:

         nix profile remove path:${flake1Dir}#packages.${system}.default

       The new package can also be installed next to the existing one by assigning a different priority.
       The conflicting packages have a priority of 5.
       To prioritise the new package:

         nix profile install path:${flake2Dir}#packages.${system}.default --priority 4

       To prioritise the existing package:

         nix profile install path:${flake2Dir}#packages.${system}.default --priority 6
EOF
)
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
nix profile install $flake2Dir --priority 100
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
nix profile install $flake2Dir --priority 0
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World2" ]]
# nix profile install $flake1Dir --priority 100
# [[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]

# Ensure that conflicts are handled properly even when the installables aren't
# flake references.
# Regression test for https://github.com/NixOS/nix/issues/8284
clearProfiles
nix profile install $(nix build $flake1Dir --no-link --print-out-paths)
expect 1 nix profile install --impure --expr "(builtins.getFlake ''$flake2Dir'').packages.$system.default"
