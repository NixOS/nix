source common.sh

drvPath=$($nixinstantiate dependencies.nix)
outPath=$($nixstore -r $drvPath)

echo "pushing $drvPath"

mkdir -p $TEST_ROOT/cache

nix-push --copy $TEST_ROOT/cache $TEST_ROOT/manifest $drvPath
