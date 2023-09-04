source common.sh

clearStore
clearProfiles

enableFeatures "ca-derivations ca-references"
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
      builder = builtins.toFile "builder.sh"
        ''
          mkdir -p \$out/bin
          cat > \$out/bin/hello <<EOF
          #! ${shell}
          echo Hello \${builtins.readFile ./who}
          EOF
          chmod +x \$out/bin/hello
          echo DONE
        '';
      __contentAddressed = import ./ca.nix;
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
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
nix profile list | grep '0 - - .*-foo-1.0'
nix profile install $flake1Dir -L
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
nix profile history
nix profile history | grep "packages.$system.default: ∅ -> 1.0"
nix profile diff-closures | grep 'env-manifest.nix: ε → ∅'

# Test upgrading a package.
printf NixOS > $flake1Dir/who
printf 2.0 > $flake1Dir/version
nix profile upgrade 1
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello NixOS" ]]
nix profile history | grep "packages.$system.default: 1.0 -> 2.0"

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
nix profile history | grep "packages.$system.default: 1.0 -> 3.0"

# Test new install of CA package.
nix profile remove 0
printf 4.0 > $flake1Dir/version
printf Utrecht > $flake1Dir/who
nix profile install $flake1Dir
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello Utrecht" ]]
[[ $(nix path-info --json $(realpath $TEST_HOME/.nix-profile/bin/hello) | jq -r .[].ca) =~ fixed:r:sha256: ]]
