source common.sh

clearStore

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME
cd $TEST_HOME
cat <<EOF > flake.nix
{
    outputs = a: {
       defaultPackage.$system = import ./simple.nix;
       packages.$system.test = import ./simple.nix;
    };
}
EOF
mkdir subdir
cd subdir

for i in "" . "$PWD" .# .#test; do
    nix build $i || fail "flake should be found by searching up directories"
done

for i in "path:$PWD"; do
    ! nix build $i || fail "flake should not search up directories when using 'path:'"
done
