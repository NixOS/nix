source ../common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local
cp ../shell-hello.nix ../config.nix $TEST_HOME
cd $TEST_HOME

mkdir -p template

cat >flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    checks.$system = { };
    devShells.$system = { };
    legacyPackages.$system = { };
    packages.$system.pkgAsPkg = (import ./shell-hello.nix).hello;
    packages.someOtherSystem = { };

    formatter = { };
    nixosConfigurations = { };
    nixosModules = { };
    templates.default = { path = ./template; };
  };
}

EOF

declare -A subtests=(
  ["nix run"]="nix run --no-write-lock-file .#pkgAsPkg"
  ["nix shell"]="nix shell -f shell-hello.nix hello -c hello | grep 'Hello World'"
  ["nix develop"]="nix develop -f shell-hello.nix hello -c echo test | grep 'test'"
  ["nix flake init"]="nix flake init -t .#"
  ["nix flake new"]="nix flake new $TEST_HOME/ -t .#"
  ["nix profile list"]="nix profile list"
  ["nix profile install"]="nix profile install .#pkgAsPkg"
  ["nix-env -q"]="nix-env -q"
)

for test_name in "${!subtests[@]}"; do
  clearStore
  clearProfiles
  rm -rf $TEST_HOME/.local

  [[ ! -e $TEST_HOME/.local ]] || fail "The nix profile should not exist yet"

  ${subtests[$test_name]}

  [[ -e $TEST_HOME/.local/state/nix/profiles ]] || fail "\'$test_name\' should create the user's default profile if it doesn't exist yet"
done
