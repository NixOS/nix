source common.sh

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -r $drvPath)

echo "pushing $drvPath"

mkdir $TEST_ROOT/cache

$NIX_BIN_DIR/nix-push \
    --copy $TEST_ROOT/cache $TEST_ROOT/manifest $drvPath
