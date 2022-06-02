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
nix profile list | grep '0 - - .*-foo-1.0'
nix profile install $flake1Dir -L
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
[ -e $TEST_HOME/.nix-profile/share/man ]
(! [ -e $TEST_HOME/.nix-profile/include ])
nix profile history
nix profile history | grep "packages.$system.default: ∅ -> 1.0"
nix profile diff-closures | grep 'env-manifest.nix: ε → ∅'

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
nix profile install $flake2Dir --priority 100
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
nix profile install $flake2Dir --priority 0
[[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World2" ]]
# nix profile install $flake1Dir --priority 100
# [[ $($TEST_HOME/.nix-profile/bin/hello) = "Hello World" ]]
