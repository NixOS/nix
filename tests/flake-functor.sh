source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME

cd $TEST_HOME

cat <<EOF > flake.nix
{
    outputs = {self}: {
      __functor = self: a: {};
      packages.$system = {
        default = import ./simple.nix;
      };
    };
}
EOF
nix eval .#packages.$system.default --json

clearStore

