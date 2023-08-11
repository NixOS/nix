source ./common.sh

requireGit

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

flakeDir=$TEST_ROOT/flake-autocall

createGitRepo "$flakeDir"

cat > $flakeDir/flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system = rec {
      foo = { name ? "simple" }: with import ./config.nix; mkDerivation {
        inherit name;
        builder = ./simple.builder.sh;
        PATH = "";
        goodPath = path;
      };
      default = foo;
    };
  };
}
EOF

cp ../simple.builder.sh ../config.nix $flakeDir/
git -C $flakeDir add flake.nix simple.builder.sh config.nix
git -C $flakeDir commit -m 'Initial'

# Build with no args set
nix build -o $TEST_ROOT/result $flakeDir
[[ $(basename $(readlink $TEST_ROOT/result) | cut -d '-' -f 2) == "simple" ]]

# Set an arg
nix build --argstr name sample -o $TEST_ROOT/result $flakeDir
[[ $(basename $(readlink $TEST_ROOT/result) | cut -d '-' -f 2) == "sample" ]]
