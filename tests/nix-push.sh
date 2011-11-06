source common.sh

drvPath=$(nix-instantiate dependencies.nix)
outPath=$(nix-store -r $drvPath)

echo "pushing $drvPath"

mkdir -p $TEST_ROOT/cache

nix-push --copy $TEST_ROOT/cache $TEST_ROOT/manifest $drvPath
