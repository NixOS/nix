source common.sh

echo example > $TEST_ROOT/example.txt
mkdir -p $TEST_ROOT/x

export NIX_STORE_DIR=/nix2/store

CORRECT_PATH=$(cd $TEST_ROOT && nix-store --store ./x --add example.txt)

[[ $CORRECT_PATH =~ ^/nix2/store/.*-example.txt$ ]]

PATH1=$(cd $TEST_ROOT && nix path-info --store ./x $CORRECT_PATH)
[ $CORRECT_PATH == $PATH1 ]

PATH2=$(nix path-info --store "$TEST_ROOT/x" $CORRECT_PATH)
[ $CORRECT_PATH == $PATH2 ]

PATH3=$(nix path-info --store "local?root=$TEST_ROOT/x" $CORRECT_PATH)
[ $CORRECT_PATH == $PATH3 ]

# Ensure store info trusted works with local store
nix --store $TEST_ROOT/x store info --json | jq -e '.trusted'

# Test building in a chroot store.
if canUseSandbox; then

    flakeDir=$TEST_ROOT/flake
    mkdir -p $flakeDir

    cat > $flakeDir/flake.nix <<EOF
{
  outputs = inputs: rec {
    packages.$system.default = import ./simple.nix;
  };
}
EOF

    cp simple.nix shell.nix simple.builder.sh config.nix $flakeDir/

    outPath=$(nix build --print-out-paths --no-link --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store $TEST_ROOT/x path:$flakeDir)

    [[ $outPath =~ ^/nix2/store/.*-simple$ ]]

    [[ $(cat $TEST_ROOT/x/nix/store/$(basename $outPath)/hello) = 'Hello World!' ]]
fi
