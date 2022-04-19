source common.sh

set -o pipefail

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./fmt.simple.sh ./config.nix $TEST_HOME

cd $TEST_HOME

nix fmt --help | grep "Format"

cat << EOF > flake.nix
{
  outputs = _: {
    formatter.$system = {
      type = "app";
      program = ./fmt.simple.sh;
    };
  };
}
EOF
nix fmt ./file ./folder | grep 'Formatting: ./file ./folder'
nix flake check
nix flake show | grep -P 'x86_64-linux|x86_64-darwin'

clearStore
