source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cd $TEST_HOME

cat <<EOF > flake.nix
{
    outputs = give me an error here;
}
EOF

nix build |& grep $TEST_HOME || fail "Path should point to home, not to the store"
