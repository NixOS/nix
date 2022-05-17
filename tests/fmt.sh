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
    formatter.$system =
      with import ./config.nix;
      mkDerivation {
        name = "formatter";
        buildCommand = "mkdir -p \$out/bin; cp \${./fmt.simple.sh} \$out/bin/formatter";
      };
  };
}
EOF
nix fmt ./file ./folder | grep 'Formatting: ./file ./folder'
nix flake check
nix flake show | grep -P "package 'formatter'"

clearStore
